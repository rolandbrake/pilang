import PiExpression from "./PiExpression.js";

export default class PiTupleExpression extends PiExpression {
  constructor(startToken, elements, commas, endToken) {
    super(startToken, endToken);
    this._startToken = startToken;
    this._elements = elements;
    this._commas = commas;
    this._endToken = endToken;
  }

  format(indent = 0) {
    const prefix = this.indent(indent) + this.formatComments(this._startToken, indent, "leading");
    if (this._elements.length === 0) return prefix + "()";

    const parts = this._elements.map((expr) => expr.format(0).trim());
    let result = prefix + "(" + parts.join(", ");
    if (this._elements.length === 1 || this._commas.length >= this._elements.length) {
      result += ",";
    }
    result += ")";
    return result;
  }

  minify(context) {
    if (this._elements.length === 0) return "()";
    let result = "(" + this._elements.map((expr) => expr.minify(context)).join(",");
    if (this._elements.length === 1 || this._commas.length >= this._elements.length) result += ",";
    return result + ")";
  }
}
