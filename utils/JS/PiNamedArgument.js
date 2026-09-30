import PiExpression from "./PiExpression.js";

export default class PiNamedArgument extends PiExpression {
  constructor(nameToken, eqToken, value) {
    super(nameToken, value.getLastToken());
    this._nameToken = nameToken;
    this._eqToken = eqToken;
    this._value = value;
  }

  format(indent = 0) {
    let result = this.indent(indent);
    result += this.formatComments(this._nameToken, indent, "leading");
    result += this._nameToken.value;
    result += this.formatComments(this._nameToken, indent, "trailing");
    result += " ";
    result += this.formatComments(this._eqToken, indent, "leading");
    result += "=";
    result += this.formatComments(this._eqToken, indent, "trailing");
    result += " " + this._value.format(0);
    return result;
  }

  minify(context) {
    return this._nameToken.value + "=" + this._value.minify(context);
  }
}
