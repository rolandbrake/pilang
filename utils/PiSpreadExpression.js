import PiExpression from "./PiExpression.js";

export default class PiSpreadExpression extends PiExpression {
  constructor(token, expression) {
    super(token, expression.getLastToken());
    this._token = token;
    this._expression = expression;
  }

  format(indent = 0) {
    return (
      this.indent(indent) +
      this.formatComments(this._token, indent, "leading") +
      "..." +
      this._expression.format(0)
    );
  }

  minify(context) {
    return "..." + this._expression.minify(context);
  }
}
