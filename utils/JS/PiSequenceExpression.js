import PiExpression from "./PiExpression.js";

export default class PiSequenceExpression extends PiExpression {
  constructor(expressions) {
    super(expressions[0].getStartToken(), expressions[expressions.length - 1].getLastToken());
    this._expressions = expressions;
  }

  format(indent = 0) {
    return this.indent(indent) + this._expressions.map((expr) => expr.format(0).trim()).join(", ");
  }

  minify(context) {
    return this._expressions.map((expr) => expr.minify(context)).join(",");
  }
}
