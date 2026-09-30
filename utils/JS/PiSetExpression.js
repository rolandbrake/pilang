import PiExpression from "./PiExpression.js";

export default class PiSetExpression extends PiExpression {
  constructor(startToken, elements, commas, endToken) {
    super(startToken, endToken);
    this._startToken = startToken;
    this._elements = elements;
    this._commas = commas;
    this._endToken = endToken;
  }

  format(indent = 0) {
    const prefix = this.indent(indent) + this.formatComments(this._startToken, indent, "leading");
    return prefix + "{" + this._elements.map((expr) => expr.format(0).trim()).join(", ") + "}";
  }

  minify(context) {
    return "{" + this._elements.map((expr) => expr.minify(context)).join(",") + "}";
  }
}
