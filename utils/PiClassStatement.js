import PiStatement from "./PiStatement.js";

export default class PiClassStatement extends PiStatement {
  constructor(classToken, nameToken, parentToken, members, endToken) {
    super(classToken, endToken);
    this._classToken = classToken;
    this._nameToken = nameToken;
    this._parentToken = parentToken;
    this._members = members;
    this._endToken = endToken;
  }

  format(indent = 0) {
    let result = this.indent(indent) + "class " + this._nameToken.value;
    if (this._parentToken) result += ":" + this._parentToken.value;
    result += " {\n";
    this._members.forEach((member, index) => {
      result += this.indent(indent + 2);
      if (member.kind === "field") {
        result += member.name.value + " = " + member.value.format(0) + ";";
      } else {
        result += member.name.value + "(";
        result += member.params.map((param) => {
          let p = param.nameToken.value;
          if (param.defaultValue) p += " = " + param.defaultValue.format(0);
          return p;
        }).join(", ");
        result += ")" + member.body.format(indent + 2, true);
      }
      if (index < this._members.length - 1) result += "\n";
    });
    result += "\n" + this.indent(indent) + "}";
    return result;
  }

  minify(context) {
    const className = context.setValue(this._nameToken.value);
    let result = "class " + className;
    if (this._parentToken) result += ":" + context.getValue(this._parentToken.value);
    result += "{";
    context.pushScope();
    result += this._members.map((member) => {
      if (member.kind === "field") return member.name.value + "=" + member.value.minify(context) + ";";
      context.pushScope();
      const params = member.params.map((param) => {
        const name = context.setValue(param.nameToken.value);
        return param.defaultValue ? name + "=" + param.defaultValue.minify(context) : name;
      }).join(",");
      const body = member.body.minify(context);
      context.popScope();
      return member.name.value + "(" + params + ")" + body;
    }).join("");
    context.popScope();
    return result + "}";
  }
}
