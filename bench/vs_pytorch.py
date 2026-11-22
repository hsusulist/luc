# PyTorch side of the lanternl vs PyTorch comparison.
# Same model as lanternl's GPU transformer: byte-level vocab, RMSNorm pre-norm,
# fused QKV with RoPE, SwiGLU FFN, tied output head, no biases, GPT-2 style init.
# Same training: AdamW (0.9, 0.95, wd 0.01 on weights only), clip 1.0,
# 5% linear warmup then cosine to 0, non-overlapping windows, last 10% = validation.
import math, random, time, torch, torch.nn as nn, torch.nn.functional as F

DIM, LAYERS, HEADS, FFN, SEQ, BATCH, EPOCHS, LR, VAL = 128, 4, 4, 256, 128, 32, 2, 3e-3, 0.1
dev = "cuda"
torch.manual_seed(1); random.seed(1)

text = open("bench/data.txt", "rb").read()
ids = torch.tensor(list(text), dtype=torch.long)  # bytes 0..255

# windows exactly like lanternl's build_windows (stride = max_seq)
wins, start, N = [], 0, len(ids)
while start < N - 1:
    stop = min(start + SEQ, N - 1)
    wins.append((start, stop))
    if stop >= N - 1: break
    start += SEQ
nval = max(1, int(len(wins) * VAL))
train_w, val_w = wins[:-nval], wins[-nval:]

def make_batch(ws):
    x = torch.zeros(len(ws), SEQ, dtype=torch.long)
    y = torch.full((len(ws), SEQ), -1, dtype=torch.long)
    for i, (a, b) in enumerate(ws):
        x[i, :b - a] = ids[a:b]; y[i, :b - a] = ids[a + 1:b + 1]
    return x.to(dev), y.to(dev)

class RMSNorm(nn.Module):
    def __init__(s, d): super().__init__(); s.w = nn.Parameter(torch.ones(d))
    def forward(s, x): return x * torch.rsqrt(x.pow(2).mean(-1, keepdim=True) + 1e-5) * s.w

def rope(x, base=10000.0):  # x: B,H,T,hd
    T, hd = x.shape[2], x.shape[3]
    inv = 1.0 / (base ** (torch.arange(0, hd, 2, device=x.device).float() / hd))
    ang = torch.arange(T, device=x.device).float()[:, None] * inv[None]
    c, s = ang.cos(), ang.sin()
    x1, x2 = x[..., 0::2], x[..., 1::2]
    return torch.stack((x1 * c - x2 * s, x1 * s + x2 * c), -1).flatten(-2)

class Block(nn.Module):
    def __init__(s):
        super().__init__()
        s.ln1, s.ln2 = RMSNorm(DIM), RMSNorm(DIM)
        s.qkv = nn.Linear(DIM, 3 * DIM, bias=False); s.wo = nn.Linear(DIM, DIM, bias=False)
        s.w13 = nn.Linear(DIM, 2 * FFN, bias=False); s.w2 = nn.Linear(FFN, DIM, bias=False)
    def forward(s, x):
        B, T, _ = x.shape
        q, k, v = s.qkv(s.ln1(x)).view(B, T, 3, HEADS, DIM // HEADS).permute(2, 0, 3, 1, 4)
        a = F.scaled_dot_product_attention(rope(q), rope(k), v, is_causal=True)
        x = x + s.wo(a.transpose(1, 2).reshape(B, T, DIM))
        g, u = s.w13(s.ln2(x)).chunk(2, -1)
        return x + s.w2(F.silu(g) * u)

class LM(nn.Module):
    def __init__(s):
        super().__init__()
        s.emb = nn.Embedding(256, DIM); s.blocks = nn.ModuleList(Block() for _ in range(LAYERS))
        s.lnf = RMSNorm(DIM)
        rstd = 0.02 / math.sqrt(2 * LAYERS)
        for n, p in s.named_parameters():
            if p.dim() == 2: nn.init.normal_(p, 0, rstd if n.endswith(("wo.weight", "w2.weight")) else 0.02)
    def forward(s, x):
        h = s.emb(x)
        for b in s.blocks: h = b(h)
        return s.lnf(h) @ s.emb.weight.T

model = LM().to(dev)
decay = [p for p in model.parameters() if p.dim() == 2]
nodecay = [p for p in model.parameters() if p.dim() < 2]
opt = torch.optim.AdamW([{"params": decay, "weight_decay": 0.01},
                         {"params": nodecay, "weight_decay": 0.0}], lr=LR, betas=(0.9, 0.95), eps=1e-8)

nb = math.ceil(len(train_w) / BATCH); total = EPOCHS * nb; warm = int(0.05 * total)
def lr_at(step):
    if warm > 0 and step <= warm: return LR * step / warm
    p = min(1.0, (step - warm) / max(1, total - warm))
    return 0.5 * LR * (1 + math.cos(math.pi * p))

@torch.no_grad()
def evaluate(ws):
    model.eval(); tot = cnt = 0
    for i in range(0, len(ws), BATCH):
        x, y = make_batch(ws[i:i + BATCH])
        l = F.cross_entropy(model(x).view(-1, 256), y.view(-1), ignore_index=-1, reduction="sum")
        tot += l.item(); cnt += (y >= 0).sum().item()
    model.train(); return tot / cnt

torch.cuda.synchronize(); t0 = time.time(); step = 0
for ep in range(EPOCHS):
    order = train_w[:]; random.shuffle(order); sl = st = 0
    for b in range(nb):
        x, y = make_batch(order[b * BATCH:(b + 1) * BATCH]); step += 1
        for g in opt.param_groups: g["lr"] = lr_at(step)
        loss = F.cross_entropy(model(x).view(-1, 256), y.view(-1), ignore_index=-1)
        opt.zero_grad(set_to_none=True); loss.backward()
        torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0); opt.step()
        n = (y >= 0).sum().item(); sl += loss.item() * n; st += n
    vl = evaluate(val_w)
    print(f"[pytorch] epoch {ep + 1}/{EPOCHS}  loss {sl / st:.4f}  val {vl:.4f}")
torch.cuda.synchronize(); secs = time.time() - t0

toks = len(text) * (1 - VAL) * EPOCHS
print(f"RESULT pytorch  | time {secs:.1f} s | {toks / secs:.0f} tok/s | train loss {sl / st:.4f} | val loss {vl:.4f}")

@torch.no_grad()
def generate(prompt, n):
    model.eval(); x = torch.tensor([list(prompt.encode())], device=dev)
    for _ in range(n):
        nxt = model(x[:, -SEQ:])[0, -1].argmax()
        x = torch.cat([x, nxt.view(1, 1)], 1)
    return bytes(x[0].tolist()).decode("utf-8", "replace")
print("SAMPLE pytorch  |")
print(generate("ROMEO:", 200))
