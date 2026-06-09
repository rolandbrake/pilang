import PiExpression from "./PiExpression.js";

export default class PiListExpression extends PiExpression {
  constructor(start, exprs, commas, end) {
    super(start, end); // Pass the start token to the base class
    this.start = start; // The '[' token
    this._exprs = exprs; // Array of PiExpression nodes
    this.commas = commas; // Array of ',' tokens
    this.end = end; // The ']' token
  }

  getSize() {
    return this._exprs.length;
  }

  format(indent = 0) {
    if (this._exprs.length === 0) {
      let result = this.indent(indent);
      result += this.formatComments(this.start, indent, "leading");
      result += "[]";
      return result;
    }

    const maxLineLength = 80;
    const innerIndent = indent + 2;
    const innerIndentStr = this.indent(innerIndent);
    const formattedItems = this._exprs.map((expr) => expr.format(0).trim());
    const hasComments = [this.start, this.end, ...this.commas].some(
      (token) =>
        token &&
        ((token.leadingComments && token.leadingComments.length > 0) ||
          (token.trailingComments && token.trailingComments.length > 0))
    );
    const hasNestedOrMultiline = formattedItems.some(
      (item) =>
        item.includes("\n") ||
        item.startsWith("[") ||
        item.startsWith("{")
    );
    const singleLine = "[" + formattedItems.join(", ") + "]";

    let result = this.indent(indent);
    result += this.formatComments(this.start, indent, "leading");

    if (!hasComments && !hasNestedOrMultiline && indent + singleLine.length <= maxLineLength) {
      return result + singleLine;
    }

    result += "[";
    const startTrailing = this.formatComments(this.start, indent, "trailing");
    if (startTrailing.trim().length > 0) {
      result += startTrailing;
    }
    result += "\n";

    if (!hasComments && !hasNestedOrMultiline) {
      const lines = [];
      let line = "";

      formattedItems.forEach((item, index) => {
        const part = item + (index < formattedItems.length - 1 ? "," : "");
        const candidate = line.length === 0 ? part : line + " " + part;
        if (line.length > 0 && innerIndent + candidate.length > maxLineLength) {
          lines.push(innerIndentStr + line);
          line = part;
        } else {
          line = candidate;
        }
      });

      if (line.length > 0) {
        lines.push(innerIndentStr + line);
      }

      result += lines.join("\n");
    } else {
      this._exprs.forEach((expr, index) => {
        const item = expr.format(innerIndent).trimEnd();
        result += item.startsWith(innerIndentStr)
          ? item
          : innerIndentStr + item;

        if (index < this._exprs.length - 1) {
          const commaToken = this.commas[index];
          result += commaToken ? this.formatComments(commaToken, 0, "leading") : "";
          result += ",";
          const trailing = commaToken ? this.formatComments(commaToken, 0, "trailing") : "";
          result += trailing;
          result += "\n";
        }
      });
    }

    result += "\n" + this.indent(indent);
    result += this.formatComments(this.end, indent, "leading");
    result += "]";
    return result;
  }

  minify(context) {
    if (this._exprs.length === 0) return "[]";

    let s = "[";
    for (let i = 0; i < this._exprs.length; i++) {
      s += this._exprs[i].minify(context);
      if (i < this._exprs.length - 1) s += ",";
    }
    s += "]";
    return s;
  }
}
