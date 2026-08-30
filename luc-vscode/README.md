# LUC Language Support

This editor extension adds:

- `.luc` file recognition
- LUC syntax highlighting
- bracket and comment support
- an optional LUC file icon theme

## Install locally in VS Code

1. Open the `luc-vscode` folder in a terminal.
2. Package it with `vsce package` after installing `@vscode/vsce`.
3. In VS Code, open **Extensions**, choose **...**, then **Install from VSIX**.
4. Open the file icon menu and select **LUC Icons**.

The LUC compiler and runtime are still used separately:

```bash
luc test.luc
```