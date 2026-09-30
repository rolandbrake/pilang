import PiExpression from "./PiExpression.js";

export default class PiFillExpression extends PiExpression {
  constructor(ellipsisToken, endpoint) {
    super(ellipsisToken, endpoint.getLastToken());
    this._ellipsisToken = ellipsisToken;
    this._endpoint = endpoint;
  }

  format(indent = 0) {
    return (
      this.indent(indent) +
      this.formatComments(this._ellipsisToken, indent, "leading") +
      "..., " +
      this._endpoint.format(0)
    );
  }

  minify(context) {
    return "...," + this._endpoint.minify(context);
  }
}
