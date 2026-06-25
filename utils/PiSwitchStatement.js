import PiStatement from "./PiStatement.js";

export default class PiSwitchStatement extends PiStatement {
  constructor(switchToken, value, cases, endToken) {
    super(switchToken, endToken);
    this._switchToken = switchToken;
    this._value = value;
    this._cases = cases;
    this._endToken = endToken;
  }

  format(indent = 0) {
    let result = this.formatComments(this._switchToken, indent, "leading");
    result += this.indent(indent);
    result += "switch ";
    result += this._value.format(0);
    result += " {";
    result += this.formatComments(this._switchToken, indent, "trailing");

    for (const entry of this._cases) {
      result += "\n";
      const label = entry.defaultToken ? "_" : entry.label.format(0);
      result += this.indent(indent + 2) + label + ":";
      result += this.formatComments(entry.colon, indent + 2, "trailing");

      if (entry.body.isBlock) {
        result += entry.body.format(indent + 2, true);
      } else {
        const body = entry.body.format(0).trim();
        if (body.includes("\n")) {
          result += "\n" + entry.body.format(indent + 4);
        } else {
          result += " " + body;
        }
      }
    }

    result += "\n" + this.indent(indent) + "}";
    result += this.formatComments(this._endToken, indent, "trailing");
    return result;
  }

  minify(context) {
    let result = "switch " + this._value.minify(context) + "{";
    for (const entry of this._cases) {
      result += entry.defaultToken ? "_:" : entry.label.minify(context) + ":";
      result += entry.body.minify(context);
    }
    result += "}";
    return result;
  }
}
