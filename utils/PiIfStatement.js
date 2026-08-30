import PiStatement from "./PiStatement.js";
import TokenType from "./TokenType.js";

export default class PiIfStatement extends PiStatement {
  constructor(
    ifToken,
    lparen,
    cond,
    rparen,
    thenStmt,
    elseToken = null,
    elseStmt = null
  ) {
    super(ifToken, (elseStmt || thenStmt).getLastToken());
    this._ifToken = ifToken;
    this._lparen = lparen;
    this._cond = cond;
    this._rparen = rparen;
    this._then = thenStmt;
    this._elseToken = elseToken;
    this._else = elseStmt;
  }

  format(indent = 0, isElifChain = false) {
    let result = "";
    const leadingComments = isElifChain
      ? ""
      : this.formatComments(this._ifToken, indent, "leading");
    if (leadingComments.length > 0) {
      result += leadingComments;
    }
    if (result.length === 0 || result.endsWith("\n")) {
      if (!isElifChain) {
        result += this.indent(indent);
      }
    }

    if (!isElifChain) {
      // Regular if statement
      result += "if";
      result += this.formatComments(this._ifToken, indent, "trailing");
      result += " "; // Space after "if"
    } else {
      // elif chain - we already have the "elif" keyword from parent
      // No space needed here, the space will be added after "elif"
    }

    result += this.formatComments(this._lparen, indent, "leading");
    result += "(";
    result += this.formatComments(this._lparen, indent, "trailing");

    result += this._cond.format(0);

    result += this.formatComments(this._rparen, indent, "leading");
    result += ")";
    result += this.formatComments(this._rparen, indent, "trailing");

    // --- THEN BODY ---
    result += this._formatBody(this._then, indent);

    // --- ELSE / ELIF ---
    if (this._else) {
      const thenIsBlock = !!this._then.isBlock;
      const leadingComments = this.formatComments(
        this._elseToken,
        indent,
        "leading"
      );

      if (leadingComments.length > 0) {
        result = result.replace(/[ \t]+$/u, "");
        if (!result.endsWith("\n")) {
          result += "\n";
        }
      } else if (thenIsBlock) {
        // Inline on same line as closing brace
        result = result.replace(/\s+$/u, ""); // Trim trailing newlines
        result += " ";
      } else {
        // Same indentation level as 'if', not deeper
        result += "\n" + this.indent(indent);
      }

      let elseBlockStr = "";
      if (leadingComments.length > 0) {
        elseBlockStr += leadingComments;
        // If leading comments end with newline, re-indent properly
        if (elseBlockStr.endsWith("\n")) {
          elseBlockStr += this.indent(indent);
        }
      }

      if (this._elseToken.type === TokenType.ELIF) {
        elseBlockStr += "elif";
        elseBlockStr += this.formatComments(
          this._elseToken,
          indent,
          "trailing"
        );
        elseBlockStr += " "; // CRITICAL: Add space after elif before condition
      } else {
        elseBlockStr += "else";
        elseBlockStr += this.formatComments(
          this._elseToken,
          indent,
          "trailing"
        );
        // A nested if is written as "else if", so keep its keyword.
        if (this._else instanceof PiIfStatement) {
          elseBlockStr += " ";
        }
      }

      result += elseBlockStr;

      if (this._else instanceof PiIfStatement) {
        // Elif chains already have their keyword from the parent. An
        // explicit "else if" needs the nested statement to emit "if".
        result += this._else
          .format(indent, this._elseToken.type === TokenType.ELIF)
          .trimStart();
      } else {
        result += this._formatBody(this._else, indent);
      }
    }

    return result;
  }

  _formatBody(stmt, indent) {
    if (stmt.isBlock) {
      return stmt.format(indent, true);
    } else {
      return "\n" + stmt.format(indent + 2);
    }
  }

  minify(context) {
    let s = "if(" + this._cond.minify(context) + ")";
    s += this._then.minify(context);

    if (this._else != null) s += this._minify(this._else, context);

    return s;
  }

  _minify(st, context) {
    if (st instanceof PiIfStatement) {
      return (
        "elif(" +
        st._cond.minify(context) +
        ")" +
        st._then.minify(context) +
        (st._else ? this._minify(st._else, context) : "")
      );
    } else return "else " + st.minify(context);
  }
}
