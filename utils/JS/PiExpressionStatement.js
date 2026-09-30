import PiStatement from "./PiStatement.js";

export default class PiExpressionStatement extends PiStatement {
  constructor(expr, semicolon = null) {
    super(expr.getStartToken(), semicolon || expr.getLastToken());
    this._expr = expr;
    this._semicolon = semicolon;
  }

  format(indent = 0) {
    let result = "";
    // Delegate formatting to the expression
    if (this._expr) {
      result += this._expr.format(indent);
    }

    if (this._semicolon) {
      result += this.formatComments(this._semicolon, indent, "leading");
      result += ";";
      result += this.formatComments(this._semicolon, indent, "trailing");
    }

    return result;
  }

  minify(context) {
    return this._expr.minify(context) + ";";
  }
}
