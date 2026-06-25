import TokenType from "./TokenType.js";
import Token from "./Token.js";
import PiCompoundStatement from "./PiCompoundStatement.js";
import PiVarDeclarations from "./PiVarDeclarations.js";
import PiVarDeclaration from "./PiVarDeclaration.js";
import PiFuncDeclStatement from "./PiFuncDeclStatement.js";
import PiFuncDeclExpression from "./PiFuncDeclExpression.js";
import PiExpressionStatement from "./PiExpressionStatement.js";
import PiObjectExpression from "./PiObjectExpression.js";
import PiBlockStatement from "./PiBlockStatement.js";
import PiIfStatement from "./PiIfStatement.js";
import PiConditionalExpression from "./PiConditionalExpression.js";
import PiWhileStatement from "./PiWhileStatement.js";
import PiForStatement from "./PiForStatement.js";
import PiReturnStatement from "./PiReturnStatement.js";
import PiAssertStatement from "./PiAssertStatement.js";
import PiBreakStatement from "./PiBreakStatement.js";
import PiContinueStatement from "./PiContinueStatement.js";
import PiUnaryExpression from "./PiUnaryExpression.js";
import PiBinaryExpression from "./PiBinaryExpression.js";
import PiLiteralExpression from "./PiLiteralExpression.js";
import PiFunctionExpression from "./PiFunctionExpression.js";
import PiListExpression from "./PiListExpression.js";
import PiRangeExpression from "./PiRangeExpression.js";
import PiSliceExpression from "./PiSliceExpression.js";
import PiVariable from "./PiVariable.js";
import ParseError from "./ParseError.js";
import PiSpreadExpression from "./PiSpreadExpression.js";
import PiNamedArgument from "./PiNamedArgument.js";
import PiTupleExpression from "./PiTupleExpression.js";
import PiSetExpression from "./PiSetExpression.js";
import PiSequenceExpression from "./PiSequenceExpression.js";
import PiImportStatement from "./PiImportStatement.js";
import PiClassStatement from "./PiClassStatement.js";
import PiSwitchStatement from "./PiSwitchStatement.js";

export default class PiParser {
  FunctionDeclarations() {
    while (!this.isAtEnd()) {
      if (this.match(TokenType.FUN)) {
        this.statements.add(this.FunctionDeclaration());
      } else {
        this.advance();
      }
    }
  }

  parse(tokens) {
    this.tokens = tokens;
    this.current = 0;
    this.access = false;
    this.isStore = false;
    this.isReturn = false;
    this.loopDepth = 0;
    this.statements = new PiCompoundStatement(null, true);
    this.key = null;
    this.last = null;
    return this.Program();
  }

  /**
   * Parses the entire source code and returns the parsed program.
   * The program is a compound statement that contains all the declarations in the source code.
   * @returns {PiCompoundStatement} Parsed program
   */

  Program() {
    while (!this.isAtEnd()) {
      let current = this.current;

      // If only comments till EOF -> stop cleanly
      if (this.isAtEnd()) {
        this.current = current;
        break;
      }

      const decl = this.Declaration();

      this.statements.add(decl);
    }

    this.statements.eofToken = this.peek();
    return this.statements;
  }

  /**
   * Parses a declaration statement.
   * It can be a variable declaration, function declaration, or a general statement.
   * @returns {PiVarDeclarations|PiFuncDeclStatement|PiStatement} Parsed statement
   */
  Declaration() {
    // Check if the declaration is a variable declaration
    if (this.match(TokenType.LET)) {
      return this.VarDeclaration(this.previous());
    }
    // Check if the declaration is a function declaration
    if (this.match(TokenType.FUN)) return this.FunctionDeclaration();
    if (this.match(TokenType.CLASS)) return this.ClassDeclaration();

    // Otherwise, parse it as a general statement
    return this.Statement();
  }

  /**
   * Parses a variable declaration.
   * @param {Token} token the token marking the start of the declaration
   * @returns {PiVarDeclarations} Parsed variable declaration
   */
  VarDeclaration(token) {
    // Parse the variable declarations
    let vars = this.Variables();

    // Consume any optional delimiters (semicolon or newline)
    const semicolon = this.match(TokenType.SEMICOLON) ? this.previous() : null;
    // Return the parsed variable declaration
    return new PiVarDeclarations(token, vars, semicolon);
  }

  /**
   * Parses a list of variables.
   * @returns {PiVarDeclaration[]} List of parsed variable declarations
   */
  Variables() {
    let vars = [];
    do {
      vars.push(this.Variable());
    } while (this.match(TokenType.COMMA));
    // Return the list of parsed variable declarations
    return vars;
  }

  /**
   * Parses a single variable declaration.
   * @returns {PiVarDeclaration} Parsed variable declaration
   */
  Variable() {
    // Parse the variable name
    let name = this.consume(TokenType.ID, "Expect variable name");

    // Parse the optional assignment expression
    let eqToken = null;
    let init = null;
    if (this.match(TokenType.ASSIGN)) {
      eqToken = this.previous();
      init = this.AssignmentExpression();
    }

    // Return the parsed variable declaration
    return new PiVarDeclaration(name, eqToken, init);
  }

  ParameterList() {
    let parameters = []; // Changed to array
    if (!this.check(TokenType.RPAREN)) {
      // If not empty parameter list
      do {
        if (parameters.length >= 32) {
          throw new Error("Can't have more than 32 parameters.");
        }
        const nameToken = this.consume(TokenType.ID, "Expect parameter name.");
        let defaultValue = null;
        if (this.match(TokenType.ASSIGN)) {
          defaultValue = this.Expression();
        }

        // Store the parameter details
        parameters.push({
          nameToken: nameToken,
          defaultValue: defaultValue,
        });

        // Check for a comma to see if there are more parameters
        if (this.match(TokenType.COMMA)) {
          // The comma token belongs to the *previous* parameter in the list
          // So, update the last parameter added with its comma token
          parameters[parameters.length - 1].commaToken = this.previous();
          if (this.check(TokenType.RPAREN)) {
            break;
          }
        } else {
          // No comma, so this is the last parameter
          break;
        }
      } while (true); // Loop until break
    }
    return parameters;
  }

  FunctionDeclaration() {
    let funToken = this.previous();

    // This is a function expression, not a declaration statement
    if (this.check(TokenType.LPAREN)) {
      this.current--;
      return this.ExpressionStatement();
    }

    let name = new PiVariable(
      this.consume(TokenType.ID, "Expect variable name")
    );

    const lparen = this.consume(
      TokenType.LPAREN,
      "Expect '(' after function name"
    );
    let params = this.ParameterList();
    const rparen = this.consume(
      TokenType.RPAREN,
      "Expect ')' after parameters."
    );
    const lbrace = this.consume(
      TokenType.LBRACE,
      "Expect '{' before function body."
    );
    let body = this.Block(lbrace);

    return new PiFuncDeclStatement(
      funToken,
      name,
      lparen,
      params,
      rparen,
      body
    );
  }

  ClassDeclaration() {
    const classToken = this.previous();
    const nameToken = this.consume(TokenType.ID, "Expect class name.");
    let parentToken = null;
    if (this.match(TokenType.COLON)) {
      parentToken = this.consume(TokenType.ID, "Expect parent class name after ':'.");
    }
    this.consume(TokenType.LBRACE, "Expect '{' before class body.");
    const members = [];
    while (!this.check(TokenType.RBRACE) && !this.isAtEnd()) {
      const memberName = this.consume(TokenType.ID, "Expect class member name.");
      if (this.match(TokenType.LPAREN)) {
        const params = this.ParameterList();
        this.consume(TokenType.RPAREN, "Expect ')' after method parameters.");
        const lbrace = this.consume(TokenType.LBRACE, "Expect '{' before method body.");
        members.push({
          kind: "method",
          name: memberName,
          params,
          body: this.Block(lbrace),
        });
      } else {
        this.consume(TokenType.ASSIGN, "Expect '=' after class field name.");
        const value = this.Expression();
        const semicolon = this.match(TokenType.SEMICOLON) ? this.previous() : null;
        members.push({ kind: "field", name: memberName, value, semicolon });
      }
    }
    const endToken = this.consume(TokenType.RBRACE, "Expect '}' after class body.");
    return new PiClassStatement(classToken, nameToken, parentToken, members, endToken);
  }

  ImportStatement() {
    const importToken = this.previous();
    const items = [];

    do {
      items.push(this.ImportItem());
    } while (this.match(TokenType.COMMA));

    const semicolon = this.match(TokenType.SEMICOLON) ? this.previous() : null;
    return new PiImportStatement(importToken, items, semicolon);
  }

  ImportItem() {
    const pathParts = [this.consume(TokenType.ID, "Expect module name after 'import'.")];
    let tail = null;

    while (this.match(TokenType.DOT)) {
      if (this.match(TokenType.MULT)) {
        tail = { kind: "wildcard", lastToken: this.previous() };
        break;
      }
      if (this.check(TokenType.LBRACE)) {
        this.next();
        const bindings = [];
        if (!this.check(TokenType.RBRACE)) {
          do {
            if (this.check(TokenType.RBRACE)) break;
            const name = this.consume(TokenType.ID, "Expect export name inside import list.");
            let alias = null;
            if (this.match(TokenType.COLON)) {
              alias = this.consume(TokenType.ID, "Expect alias name after ':'.");
            }
            bindings.push({ name, alias });
          } while (this.match(TokenType.COMMA));
        }
        const rbrace = this.consume(TokenType.RBRACE, "Expect '}' after import list.");
        tail = { kind: "braced", bindings, lastToken: rbrace };
        break;
      }
      pathParts.push(this.consume(TokenType.ID, "Expect identifier after '.'."));
    }

    if (!tail && this.match(TokenType.COLON)) {
      const alias = this.consume(TokenType.ID, "Expect alias name after ':'.");
      tail = { kind: "alias", alias, lastToken: alias };

      if (this.match(TokenType.DOT)) {
        if (this.match(TokenType.MULT)) {
          tail.selector = { kind: "wildcard", lastToken: this.previous() };
          tail.lastToken = tail.selector.lastToken;
        } else if (this.check(TokenType.LBRACE)) {
          this.next();
          const bindings = [];
          if (!this.check(TokenType.RBRACE)) {
            do {
              if (this.check(TokenType.RBRACE)) break;
              const name = this.consume(TokenType.ID, "Expect export name inside import list.");
              let bindingAlias = null;
              if (this.match(TokenType.COLON)) {
                bindingAlias = this.consume(TokenType.ID, "Expect alias name after ':'.");
              }
              bindings.push({ name, alias: bindingAlias });
            } while (this.match(TokenType.COMMA));
          }
          const rbrace = this.consume(TokenType.RBRACE, "Expect '}' after import list.");
          tail.selector = { kind: "braced", bindings, lastToken: rbrace };
          tail.lastToken = rbrace;
        } else {
          throw new ParseError("Expect '*' or '{' after aliased import selector.", this.peek().line, this.peek().column);
        }
      }
    }

    return { pathParts, tail, lastToken: (tail && tail.lastToken) || pathParts[pathParts.length - 1] };
  }

  Statement() {
    if (this.match(TokenType.LBRACE)) {
      // Look ahead to check if it's an object literal (key: value format)
      const current = this.current; // Save current position

      if (
        this.match(
          TokenType.STR,
          TokenType.ID,
          TokenType.NUM,
          TokenType.FALSE,
          TokenType.TRUE
        ) &&
        this.match(TokenType.COLON)
      ) {
        // If we find key-value pattern, reset position and parse as object
        this.current = current - 1;
        return this.ExpressionStatement();
      } else {
        // Otherwise, parse as a block
        this.current = current; // Restore position
        return this.Block();
      }
    } else if (this.match(TokenType.IF)) {
      return this.IfStatement();
    } else if (this.match(TokenType.SWITCH)) {
      return this.SwitchStatement();
    } else if (this.match(TokenType.WHILE)) {
      return this.WhileStatement();
    } else if (this.match(TokenType.FOR)) {
      return this.ForStatement();
    } else if (this.match(TokenType.BREAK)) {
      return this.BreakStatement();
    } else if (this.match(TokenType.CONTINUE)) {
      return this.ContinueStatement();
    } else if (this.match(TokenType.RETURN)) {
      return this.ReturnStatement();
    } else if (this.match(TokenType.ASSERT)) {
      return this.AssertStatement();
    } else if (this.match(TokenType.DEBUG)) {
      return this.DebugStatement();
    } else if (this.match(TokenType.IMPORT)) {
      return this.ImportStatement();
    } else return this.ExpressionStatement();
  }

  CompoundStatement() {
    let token = this.previous();
    let statements = new PiCompoundStatement(token);
    while (!this.check(TokenType.RBRACE) && !this.isAtEnd()) {
      statements.add(this.Statement());
    }
    this.consume(TokenType.RBRACE, "Expect '}' after block.");
    return statements;
  }
  Block() {
    let lbrace = this.previous();
    let statements = new PiCompoundStatement(lbrace);
    while (!this.check(TokenType.RBRACE) && !this.isAtEnd()) {
      statements.add(this.Declaration());
    }
    let rbrace = this.consume(TokenType.RBRACE, "Expect '}' after block.");
    return new PiBlockStatement(lbrace, statements, rbrace);
  }

  AssertStatement() {
    let token = this.previous();
    let value = this.Expression();
    const semicolon = this.match(TokenType.SEMICOLON) ? this.previous() : null;
    return new PiAssertStatement(token, value, semicolon);
  }

  DebugStatement() {
    let token = this.previous();
    let value = this.Expression();
    const semicolon = this.match(TokenType.SEMICOLON) ? this.previous() : null;
    return new PiDebugStatement(token, value, semicolon);
  }

  /**
   * Parses an 'if' statement.
   * Handles optional 'elif' and 'else' clauses.
   * @returns {PiIfStatement} Parsed if statement
   */
  IfStatement() {
    let ifToken = this.previous();

    const lparen = this.match(TokenType.LPAREN) ? this.previous() : null;
    let cond = this.Expression();
    let rparen = null;
    if (lparen) {
      rparen = this.consume(TokenType.RPAREN, "Expect ')' after condition.");
    }

    let thenStmt;
    if (this.match(TokenType.LBRACE)) {
      thenStmt = this.Block();
    } else {
      thenStmt = this.Statement();
    }

    let elseToken = null;
    let elseStmt = null;

    if (this.match(TokenType.ELIF)) {
      elseToken = this.previous();
      elseStmt = this.IfStatement();
    } else if (this.match(TokenType.ELSE)) {
      elseToken = this.previous();
      elseStmt = this.Statement();
    }

    return new PiIfStatement(
      ifToken,
      lparen,
      cond,
      rparen,
      thenStmt,
      elseToken,
      elseStmt
    );
  }

  SwitchStatement() {
    const switchToken = this.previous();
    const value = this.Expression();
    this.consume(TokenType.LBRACE, "Expect '{' before switch cases.");

    const cases = [];
    let sawDefault = false;

    while (!this.check(TokenType.RBRACE) && !this.isAtEnd()) {
      if (sawDefault) {
        throw new ParseError(
          "Default switch case '_' must be the last case.",
          this.peek().line,
          this.peek().column
        );
      }

      let label = null;
      let defaultToken = null;

      if (
        this.check(TokenType.ID) &&
        this.peek().value === "_" &&
        this.checkNext(TokenType.COLON)
      ) {
        defaultToken = this.next();
        sawDefault = true;
      } else {
        label = this.SwitchCaseExpression();
      }

      const colon = this.consume(TokenType.COLON, "Expect ':' after switch case.");
      let body;
      if (this.match(TokenType.LBRACE)) {
        body = this.Block();
      } else {
        body = this.Statement();
      }

      cases.push({ label, defaultToken, colon, body });
    }

    const endToken = this.consume(TokenType.RBRACE, "Expect '}' after switch cases.");
    return new PiSwitchStatement(switchToken, value, cases, endToken);
  }

  SwitchCaseExpression() {
    const colonIndex = this.findSwitchCaseColon();
    if (colonIndex < 0) {
      throw new ParseError(
        "Expect ':' after switch case condition.",
        this.peek().line,
        this.peek().column
      );
    }

    const savedCurrent = this.current;
    const savedType = this.tokens[colonIndex].type;
    let expression;
    try {
      this.tokens[colonIndex].type = TokenType.EOF;
      expression = this.Expression();
    } finally {
      this.tokens[colonIndex].type = savedType;
    }

    if (this.current !== colonIndex) {
      const token = this.peek();
      this.current = savedCurrent;
      throw new ParseError(
        "Invalid switch case condition.",
        token.line,
        token.column
      );
    }

    return expression;
  }

  findSwitchCaseColon() {
    let index = this.current;
    let parenDepth = 0;
    let bracketDepth = 0;
    let braceDepth = 0;
    let ternaryDepth = 0;

    while (this.tokens[index].type !== TokenType.EOF) {
      const token = this.tokens[index];
      switch (token.type) {
        case TokenType.LPAREN:
          parenDepth++;
          break;
        case TokenType.RPAREN:
          if (parenDepth > 0) parenDepth--;
          break;
        case TokenType.LBRACKET:
          bracketDepth++;
          break;
        case TokenType.RBRACKET:
          if (bracketDepth > 0) bracketDepth--;
          break;
        case TokenType.LBRACE:
          braceDepth++;
          break;
        case TokenType.RBRACE:
          if (braceDepth > 0) braceDepth--;
          else return -1;
          break;
        case TokenType.QUESTION:
          if (parenDepth === 0 && bracketDepth === 0 && braceDepth === 0) {
            ternaryDepth++;
          }
          break;
        case TokenType.COLON:
          if (parenDepth === 0 && bracketDepth === 0 && braceDepth === 0) {
            if (ternaryDepth > 0) ternaryDepth--;
            else return index;
          }
          break;
      }
      index++;
    }

    return -1;
  }

  /**
   * Parses a 'while' statement.
   * @returns {PiWhileStatement} Parsed while statement
   */
  WhileStatement() {
    let token = this.previous();
    const lparen = this.match(TokenType.LPAREN) ? this.previous() : null;
    let cond = this.Expression();
    let rparen = null;
    if (lparen) {
      rparen = this.consume(TokenType.RPAREN, "Expect ')' after condition.");
    }
    let body;

    this.pushLoop();
    // Check if the loop body is enclosed in braces and parse accordingly
    if (this.match(TokenType.LBRACE)) {
      // If the body is enclosed in braces, parse as a block
      body = this.Block();
    } else {
      // Otherwise, parse the body as a single statement
      body = this.Statement();
    }
    this.popLoop();
    return new PiWhileStatement(token, lparen, cond, rparen, body);
  }

  /**
   * Parses a 'for' statement.
   * @returns {PiForStatement} Parsed for statement
   */
  ForStatement() {
    let forToken = this.previous();

    const lparen = this.match(TokenType.LPAREN) ? this.previous() : null;

    let init;
    // Parse the left-hand side of the for-loop
    if (this.match(TokenType.ID)) {
      init = new PiVariable(this.previous());
    } else {
      throw new Error("Invalid for-loop left-hand side. Expect identifier.");
    }

    // Consume the 'in' keyword
    const inToken = this.consume(
      TokenType.IN,
      "Expect 'in' keyword after loop variable."
    );
    // Parse the right-hand side of the for-loop
    let expr = this.Expression();

    let rparen = null;
    if (lparen) {
      // Consume the ')'
      rparen = this.consume(
        TokenType.RPAREN,
        "Expect ')' after iterable expression."
      );
    }

    let body;
    this.pushLoop();
    // Check if the loop body is enclosed in braces and parse accordingly
    if (this.match(TokenType.LBRACE)) {
      body = this.Block();
    } else {
      body = this.Statement();
    }
    this.popLoop();

    return new PiForStatement(
      forToken,
      lparen,
      init,
      inToken,
      expr,
      rparen,
      body
    );
  }

  BreakStatement() {
    let token = this.previous();

    // Check if we're inside a loop
    if (!this.inLoop()) {
      throw new ParseError(
        "'break' used outside of a loop",
        token.line,
        token.column
      );
    }
    const semicolon = this.match(TokenType.SEMICOLON) ? this.previous() : null;
    return new PiBreakStatement(token, semicolon);
  }

  ContinueStatement() {
    let token = this.previous();

    // Check if we're inside a loop
    if (!this.inLoop()) {
      throw new ParseError(
        "'continue' used outside of a loop",
        token.line,
        token.column
      );
    }
    const semicolon = this.match(TokenType.SEMICOLON) ? this.previous() : null;
    return new PiContinueStatement(token, semicolon);
  }

  /**
   * Parses a 'return' statement.
   * @returns {PiReturnStatement} Parsed return statement
   */
  ReturnStatement() {
    let token = this.previous();
    let value = null;
    // Parse the expression that the return statement is returning
    if (!this.check(TokenType.SEMICOLON) && !this.check(TokenType.RBRACE)) {
      value = this.Expression();
    }

    const semicolon = this.match(TokenType.SEMICOLON) ? this.previous() : null;

    // Return the parsed return statement
    return new PiReturnStatement(token, value, semicolon);
  }

  /**
   * Parses an expression statement.
   * @returns {PiExpressionStatement} Parsed expression statement
   */
  ExpressionStatement() {
    let expression = this.Expression();

    // Consume any optional delimiters (semicolon or newline)
    const semicolon = this.match(TokenType.SEMICOLON) ? this.previous() : null;
    this.consumeIfExist(TokenType.NEWLINE);
    // Create a new expression statement with the expression and its location
    return new PiExpressionStatement(expression, semicolon);
  }

  /**
   * Parses an expression.
   * Delegates to AssignmentExpression for parsing assignment operations.
   * @returns {PiExpression} Parsed expression
   */

  Expression() {
    // Parse the assignment expression
    return this.AssignmentExpression();
  }

  /**
   * Parses an assignment expression.
   * Delegates to ConditionalExpression for the left-hand side.
   * @returns {PiExpression} Parsed assignment expression or conditional expression
   * @throws {Error} If the assignment target is invalid
   */
  AssignmentExpression() {
    // Parse the left-hand side of the assignment
    let expression = this.ConditionalExpression();

    // Check for assignment operators
    if (
      this.match(
        TokenType.ASSIGN,
        TokenType.PLUS_ASSIGN,
        TokenType.MINUS_ASSIGN,
        TokenType.DIV_ASSIGN,
        TokenType.MULT_ASSIGN,
        TokenType.MOD_ASSIGN,
        TokenType.BITOR_ASSIGN,
        TokenType.XOR_ASSIGN,
        TokenType.BITAND_ASSIGN,
        TokenType.LSHIFT_ASSIGN,
        TokenType.RSHIFT_ASSIGN,
        TokenType.URSHIFT_ASSIGN,
        TokenType.POWER_ASSIGN,
        TokenType.DOT_PROD_ASSIGN,
        TokenType.AND_ASSIGN,
        TokenType.OR_ASSIGN
      )
    ) {
      // Store the assignment operator
      let operator = this.previous();
      // Parse the right-hand side of the assignment
      let right = this.AssignmentExpression();

      // Ensure the left-hand side is a valid assignment target
      if (expression instanceof PiVariable) {
        let varNode = expression;
        // Return the parsed binary assignment expression
        return new PiBinaryExpression(varNode, operator, right);
      }

      // Throw an error if the assignment target is invalid
      throw new ParseError(
        "Invalid assignment target",
        expression.getLine(),
        expression.getColumn()
      );
      // throw new Error("Invalid assignment target");
    }

    // Return the parsed expression if not an assignment
    return expression;
  }

  /**
   * Parses a conditional expression.
   * @returns {PiExpression} The parsed expression
   */
  ConditionalExpression() {
    let expression = this.LogicalOrExpression();
    // Check if the expression is a conditional expression
    if (this.match(TokenType.QUESTION)) {
      // Consume the '?' token
      let t = this.previous();
      // Parse the 'then' expression
      let _then = this.Expression();
      // Consume the ':' token
      this.consume(TokenType.COLON, "Expect ':' after '?'");
      // Parse the 'else' expression
      let _else = this.ConditionalExpression();
      // Construct a new conditional expression

      expression = new PiConditionalExpression(t, expression, _then, _else);
    }
    // Return the parsed expression
    return expression;
  }

  LogicalOrExpression() {
    let expression = this.LogicalAndExpression();
    while (this.match(TokenType.OR)) {
      let operator = this.previous();
      let right = this.LogicalAndExpression();
      expression = new PiBinaryExpression(expression, operator, right);
    }
    return expression;
  }

  LogicalAndExpression() {
    let expression = this.IncludeExpression();
    while (this.match(TokenType.AND)) {
      let operator = this.previous();
      let right = this.IncludeExpression();
      expression = new PiBinaryExpression(expression, operator, right);
    }
    return expression;
  }

  IncludeExpression() {
    let expression = this.RangeExpression();
    while (this.match(TokenType.IN)) {
      let operator = this.previous();
      let right = this.RangeExpression();
      expression = new PiBinaryExpression(expression, operator, right);
    }
    return expression;
  }

  RangeExpression() {
    let expression = this.BitwiseOrExpression();
    if (this.match(TokenType.DBDOTS)) {
      let operator = this.previous();
      let right = this.BitwiseOrExpression();
      if (this.match(TokenType.COLON)) {
        let step = this.Expression();
        return new PiRangeExpression(operator, expression, right, step);
      } else {
        return new PiRangeExpression(operator, expression, right);
      }
    } else {
      return expression;
    }
  }

  BitwiseOrExpression() {
    let expression = this.BitwiseXorExpression();
    while (this.match(TokenType.BITOR)) {
      let operator = this.previous();
      let right = this.BitwiseXorExpression();
      expression = new PiBinaryExpression(expression, operator, right);
    }
    return expression;
  }

  BitwiseXorExpression() {
    let expression = this.BitwiseAndExpression();
    while (this.match(TokenType.XOR)) {
      let operator = this.previous();
      let right = this.BitwiseAndExpression();
      expression = new PiBinaryExpression(expression, operator, right);
    }
    return expression;
  }

  BitwiseAndExpression() {
    let expression = this.ShiftExpression();
    while (this.match(TokenType.BITAND)) {
      let operator = this.previous();
      let right = this.ShiftExpression();
      expression = new PiBinaryExpression(expression, operator, right);
    }
    return expression;
  }

  ShiftExpression() {
    let expression = this.EqualityExpression();
    while (this.match(TokenType.LSHIFT, TokenType.RSHIFT, TokenType.URSHIFT)) {
      let operator = this.previous();
      let right = this.ShiftExpression();
      expression = new PiBinaryExpression(expression, operator, right);
    }
    return expression;
  }

  EqualityExpression() {
    let expression = this.ComparisonExpression();
    while (this.match(TokenType.NOT_EQUAL, TokenType.EQUAL, TokenType.IS)) {
      let operator = this.previous();
      let right = this.ComparisonExpression();
      expression = new PiBinaryExpression(expression, operator, right);
    }
    return expression;
  }

  // ComparisonExpression -> AdditionExpression ( ( ">" | ">=" | "<" | "<=" ) AdditionExpression )*
  ComparisonExpression() {
    let expression = this.AdditionExpression();

    while (
      this.match(
        TokenType.GREATER,
        TokenType.GREATER_EQUAL,
        TokenType.LESS,
        TokenType.LESS_EQUAL
      )
    ) {
      const operator = this.previous();
      let right = this.AdditionExpression();

      // Build a binary expression with the current comparison operator and right-hand side
      expression = new PiBinaryExpression(expression, operator, right);

      // Check if there's another comparison operator
      while (
        this.match(
          TokenType.GREATER,
          TokenType.GREATER_EQUAL,
          TokenType.LESS,
          TokenType.LESS_EQUAL
        )
      ) {
        // Build a binary expression with the previous operator and right-hand side
        let _right = right;
        const operator = this.previous();
        right = this.AdditionExpression();
        _right = new PiBinaryExpression(_right, operator, right);

        // Build a binary expression with the logical operator and previous binary expression
        expression = new PiBinaryExpression(
          expression,
          new Token(TokenType.AND, "&&", operator.line, operator.column),
          _right
        );
      }
    }

    return expression;
  }

  // AdditionExpression -> MultiplicationExpression ( ( "-" | "+" ) MultiplicationExpression )*
  AdditionExpression() {
    let expression = this.DotProductExpression();

    while (this.match(TokenType.MINUS, TokenType.PLUS)) {
      const operator = this.previous();
      const right = this.DotProductExpression();
      expression = new PiBinaryExpression(expression, operator, right);
    }

    return expression;
  }

  // DotProductExpression -> MultiplicationExpression ( "." MultiplicationExpression )*
  DotProductExpression() {
    let expression = this.MultiplicationExpression();

    while (this.match(TokenType.DOT_PROD)) {
      const operator = this.previous();
      const right = this.MultiplicationExpression();
      expression = new PiBinaryExpression(expression, operator, right);
    }

    return expression;
  }

  // MultiplicationExpression -> ExponentiationExpression ( ( "/" | "*" | "%" ) ExponentiationExpression )*
  MultiplicationExpression() {
    let expression = this.ExponentiationExpression();

    while (this.match(TokenType.DIV, TokenType.MULT, TokenType.MOD)) {
      const operator = this.previous();
      const right = this.ExponentiationExpression();
      expression = new PiBinaryExpression(expression, operator, right);
    }

    return expression;
  }

  // ExponentiationExpression -> UnaryExpression ( "**" UnaryExpression )*
  ExponentiationExpression() {
    let expression = this.UnaryExpression();

    while (this.match(TokenType.POWER)) {
      const operator = this.previous();
      const right = this.ExponentiationExpression();
      expression = new PiBinaryExpression(expression, operator, right);
    }

    return expression;
  }

  // UnaryExpression -> ( "!" | "-" | "+" | "~" | "#" | "++" | "--" | "typeof" ) UnaryExpression | PrimaryExpression
  UnaryExpression() {
    let operator, expression;

    if (
      this.match(
        TokenType.PLUS,
        TokenType.MINUS,
        TokenType.NOT,
        TokenType.BITNEG,
        TokenType.HASH,
        TokenType.INCR,
        TokenType.DECR,
        TokenType.TYPEOF
      )
    ) {
      operator = this.previous();

      if (operator.type === TokenType.MINUS && this.match(TokenType.NUM)) {
        const number = this.previous();
        return new PiLiteralExpression(
          new Token(
            TokenType.NUM,
            -1 * number.value,
            number.line,
            number.column
          )
        );
      } else {
        expression = this.MemberExpression();

        if (
          (operator.type == TokenType.INCR ||
            operator.type == TokenType.DECR) &&
          (expression instanceof PiFunctionExpression ||
            expression instanceof PiLiteralExpression)
        ) {
          throw new ParseError(
            "Increment/Decrement operations cannot be applied to calls or literals.",
            this.last.line,
            this.last.column
          );
        }

        return new PiUnaryExpression(operator, expression, true, this.access);
      }
    } else {
      expression = this.MemberExpression();

      if (this.match(TokenType.INCR, TokenType.DECR)) {
        if (
          expression instanceof PiFunctionExpression ||
          expression instanceof PiLiteralExpression
        ) {
          throw new ParseError(
            "Unary increment/decrement operators cannot be applied to function calls or literals",
            this.last.line,
            this.last.column
          );
        }
        operator = this.previous();
        return new PiUnaryExpression(operator, expression, false, this.access);
      } else {
        return expression;
      }
    }
  }

  // MemberExpression -> PrimaryExpression "." IDENTIFIER | PrimaryExpression "[" Expression "]" | PrimaryExpression "(" ArgumentList* ")"
  MemberExpression() {
    let expression = this.PrimaryExpression();
    let container, token;

    while (true) {
      if (this.match(TokenType.DOT)) {
        token = this.previous();
        const name = new PiLiteralExpression(this.next());
        expression = new PiBinaryExpression(expression, token, name);

        if (
          this.match(
            TokenType.ASSIGN,
            TokenType.PLUS_ASSIGN,
            TokenType.MINUS_ASSIGN,
            TokenType.DIV_ASSIGN,
            TokenType.MULT_ASSIGN,
            TokenType.MOD_ASSIGN,
            TokenType.BITOR_ASSIGN,
            TokenType.XOR_ASSIGN,
            TokenType.BITAND_ASSIGN,
            TokenType.LSHIFT_ASSIGN,
            TokenType.RSHIFT_ASSIGN,
            TokenType.URSHIFT_ASSIGN,
            TokenType.POWER_ASSIGN,
            TokenType.DOT_PROD_ASSIGN,
            TokenType.AND_ASSIGN,
            TokenType.OR_ASSIGN
          )
        ) {
          token = this.previous();
          const right = this.AssignmentExpression();
          return new PiBinaryExpression(expression, token, right);
        }
        this.access = true;
      } else if (this.match(TokenType.LBRACKET)) {
        token = this.previous(); // '[' token
        const index = this.SliceExpression();
        const rbracket = this.consume(
          TokenType.RBRACKET,
          "Expect ']' after list index expression"
        );
        expression = new PiBinaryExpression(expression, token, index, rbracket);

        if (
          this.match(
            TokenType.ASSIGN,
            TokenType.PLUS_ASSIGN,
            TokenType.MINUS_ASSIGN,
            TokenType.DIV_ASSIGN,
            TokenType.MULT_ASSIGN,
            TokenType.MOD_ASSIGN,
            TokenType.BITOR_ASSIGN,
            TokenType.XOR_ASSIGN,
            TokenType.BITAND_ASSIGN,
            TokenType.LSHIFT_ASSIGN,
            TokenType.RSHIFT_ASSIGN,
            TokenType.URSHIFT_ASSIGN,
            TokenType.POWER_ASSIGN,
            TokenType.DOT_PROD_ASSIGN,
            TokenType.AND_ASSIGN,
            TokenType.OR_ASSIGN
          )
        ) {
          token = this.previous();
          const right = this.AssignmentExpression();
          return new PiBinaryExpression(expression, token, right);
        }
        this.access = true;
      } else if (this.match(TokenType.LPAREN)) {
        let _current = this.current - 1;
        let args = [];
        let start = this.previous();

        if (!this.check(TokenType.RPAREN)) {
          args = this.ArgumentList();
        }
        let end = this.consume(
          TokenType.RPAREN,
          "Expect ')' after function call"
        );
        if (this.check(TokenType.RARROW)) {
          this.current = _current;
          break;
        }
        expression = new PiFunctionExpression(start, end, expression, args);
      } else break;
    }

    return expression;
  }

  ArgumentList() {
    let args = [];
    if (this.check(TokenType.RPAREN)) return args;
    do {
      if (this.check(TokenType.RPAREN)) break;
      if (this.match(TokenType.ELLIPSIS)) {
        args.push(new PiSpreadExpression(this.previous(), this.AssignmentExpression()));
      } else if (this.check(TokenType.ID) && this.checkNext(TokenType.ASSIGN)) {
        const name = this.next();
        const eqToken = this.consume(TokenType.ASSIGN);
        args.push(new PiNamedArgument(name, eqToken, this.AssignmentExpression()));
      } else {
        args.push(this.AssignmentExpression());
      }
    } while (this.match(TokenType.COMMA));

    return args;
  }

  SliceExpression() {
    let start = null,
      step = null,
      end = null;

    const startToken = this.peek();

    if (!this.check(TokenType.COLON) && !this.check(TokenType.COMMA)) {
      start = this.ConditionalExpression();
    }

    let lastToken = start ? start.getLastToken() : startToken;

    if (this.match(TokenType.COLON)) {
      lastToken = this.previous();
      if (!this.check(TokenType.RBRACKET) && !this.check(TokenType.COLON)) {
        end = this.ConditionalExpression();
        lastToken = end.getLastToken();
      }
      if (this.match(TokenType.COLON)) {
        lastToken = this.previous();
        if (!this.check(TokenType.RBRACKET)) {
          step = this.ConditionalExpression();
          lastToken = step.getLastToken();
        }
      }
    } else {
      if (this.match(TokenType.COMMA)) {
        const expressions = [start];
        do {
          if (this.check(TokenType.RBRACKET)) break;
          expressions.push(this.SliceExpression());
        } while (this.match(TokenType.COMMA));
        return new PiSequenceExpression(expressions);
      }
      return start;
    }

    const slice = new PiSliceExpression(startToken, lastToken, start, end, step);
    if (this.match(TokenType.COMMA)) {
      const expressions = [slice];
      do {
        if (this.check(TokenType.RBRACKET)) break;
        expressions.push(this.SliceExpression());
      } while (this.match(TokenType.COMMA));
      return new PiSequenceExpression(expressions);
    }

    return slice;
  }

  PrimaryExpression() {
    if (
      this.match(
        TokenType.NUM,
        TokenType.STR,
        TokenType.TRUE,
        TokenType.FALSE,
        TokenType.NIL,
        TokenType.INF,
        TokenType.NAN,
        TokenType.SUPER
      )
    ) {
      return new PiLiteralExpression(this.previous());
    }

    if (this.match(TokenType.LPAREN)) {
      const lparen = this.previous();
      const arrowStart = this.current;
      try {
        const params = this.ParameterList();
        this.consume(TokenType.RPAREN, "Expect ')' after parameters.");
        if (this.match(TokenType.RARROW)) {
          let body;
          if (this.match(TokenType.LBRACE)) {
            body = this.Block(this.previous());
          } else {
            const returnExpr = this.Expression();
            body = new PiCompoundStatement(returnExpr.getStartToken());
            body.add(new PiReturnStatement(returnExpr.getStartToken(), returnExpr, null, true));
          }
          return new PiFuncDeclExpression(lparen, params, body, null, true);
        }
      } catch (e) {
        // Fall through to grouped/tuple parsing below.
      }

      this.current = arrowStart;
      if (this.match(TokenType.RPAREN)) {
        return new PiTupleExpression(lparen, [], [], this.previous());
      }

      const elements = [];
      const commas = [];
      elements.push(this.Expression());
      while (this.match(TokenType.COMMA)) {
        commas.push(this.previous());
        if (this.check(TokenType.RPAREN)) break;
        elements.push(this.Expression());
      }
      const rparen = this.consume(TokenType.RPAREN, "Expect ')' after expression or tuple literal.");
      if (commas.length > 0) {
        return new PiTupleExpression(lparen, elements, commas, rparen);
      }
      return elements[0];
    }

    if (this.match(TokenType.ID)) {
      const token = this.previous();
      // Simplified lambda syntax, e.g., x -> x * 2
      if (this.match(TokenType.RARROW)) {
        const params = [{ nameToken: token, defaultValue: null, commaToken: null }];
        const returnExpr = this.Expression();
        const body = new PiCompoundStatement(returnExpr.getStartToken());
        body.add(
          new PiReturnStatement(
            returnExpr.getStartToken(),
            returnExpr,
            null,
            true
          )
        );
        return new PiFuncDeclExpression(token, params, body, null, true);
      }
      return new PiVariable(token);
    }

    if (this.match(TokenType.LBRACKET)) {
      const startToken = this.previous();
      const elements = [];
      const commas = [];
      if (!this.check(TokenType.RBRACKET)) {
        do {
          // Handle trailing comma case: [a, b,]
          if (this.check(TokenType.RBRACKET)) {
            break;
          }
          if (this.match(TokenType.ELLIPSIS)) {
            elements.push(new PiSpreadExpression(this.previous(), this.Expression()));
          } else {
            elements.push(this.Expression());
          }
          if (this.match(TokenType.COMMA)) {
            commas.push(this.previous());
          } else {
            break; // No comma, so it must be the last element
          }
        } while (!this.isAtEnd());
      }
      const endToken = this.consume(
        TokenType.RBRACKET,
        "Expect ']' at the end of list literal."
      );
      return new PiListExpression(startToken, elements, commas, endToken);
    }

    // Object literal parsing
    if (this.match(TokenType.LBRACE)) {
      const startToken = this.previous();
      const properties = [];

      if (!this.check(TokenType.RBRACE)) {
        if (!this.isMapLiteralAhead()) {
          const elements = [];
          const commas = [];
          do {
            if (this.check(TokenType.RBRACE)) break;
            elements.push(this.Expression());
            if (this.match(TokenType.COMMA)) {
              commas.push(this.previous());
            } else {
              break;
            }
          } while (!this.isAtEnd());
          const endToken = this.consume(
            TokenType.RBRACE,
            "Expect '}' at the end of set literal."
          );
          return new PiSetExpression(startToken, elements, commas, endToken);
        }

        do {
          if (this.check(TokenType.RBRACE)) break; // Dangling comma

          if (this.match(TokenType.ELLIPSIS)) {
            const spread = new PiSpreadExpression(this.previous(), this.Expression());
            let commaToken = null;
            if (this.match(TokenType.COMMA)) {
              commaToken = this.previous();
            }
            properties.push({ spread, commaToken });
            if (commaToken === null) break;
            continue;
          }

          let keyToken;
          // Allow ID, STR, NUM, FALSE, TRUE for keys
          if (
            this.match(
              TokenType.STR,
              TokenType.ID,
              TokenType.NUM,
              TokenType.FALSE,
              TokenType.TRUE
            )
          ) {
            keyToken = this.previous();
            let key = keyToken.value.toString();
            if (key.length > 2 && key.substring(key.length - 2) === ".0") {
              key = key.substring(0, key.length - 2);
              keyToken = new Token(
                TokenType.NUM,
                parseFloat(key),
                keyToken.line,
                keyToken.column
              );
            }
          } else {
            throw new ParseError(
              "Expect key in object literal (identifier, string, or number).",
              this.peek()
            );
          }

          let value;
          let colonToken = null;

          // Check for method shorthand: key(params) { body }
          if (this.match(TokenType.LPAREN)) {
            /**
             * Parse a function expression as a value in the map.
             * The function expression is parsed as a lambda function
             * so it can be used as a value in the map.
             */
            const params = this.ParameterList();
            this.consume(TokenType.RPAREN, "Expect ')' after parameters.");

            let lbrace = this.consume(
              TokenType.LBRACE,
              "Expect '{' before function body."
            );
            const statements = new PiCompoundStatement(lbrace);
            while (!this.check(TokenType.RBRACE) && !this.isAtEnd()) {
              statements.add(this.Declaration());
            }
            const rbrace = this.consume(TokenType.RBRACE, "Expect '}' after function body.");
            const body = new PiBlockStatement(lbrace, statements, rbrace);

            value = new PiFuncDeclExpression(
              keyToken,
              params,
              body,
              this.key,
              false, // isArrow = false
              true // isMethod = true
            );
          } else {
            // Regular property
            colonToken = this.consume(
              TokenType.COLON,
              "Expect ':' after key in object literal."
            );
            value = this.Expression();
          }

          let commaToken = null;
          if (this.match(TokenType.COMMA)) {
            commaToken = this.previous();
          }

          properties.push({ keyToken, colonToken, value, commaToken });

          if (commaToken === null) {
            break; // Last property
          }
        } while (!this.isAtEnd());
      }
      const endToken = this.consume(
        TokenType.RBRACE,
        "Expect '}' at the end of object literal."
      );
      this.key = null;
      return new PiObjectExpression(startToken, properties, endToken);
    }

    if (this.match(TokenType.FUN)) {
      const funToken = this.previous();
      const lparen = this.consume(
        TokenType.LPAREN,
        "Expect '(' after function name"
      );
      const params = this.ParameterList();
      const rparen = this.consume(
        TokenType.RPAREN,
        "Expect ')' after parameters."
      );
      const lbrace = this.consume(
        TokenType.LBRACE,
        "Expect '{' before function body."
      );
      const body = this.Block(lbrace);
      return new PiFuncDeclExpression(funToken, params, body);
    }

    throw new ParseError(
      "Expect expression.",
      this.peek().line,
      this.peek().column
    );
  }

  isMapLiteralAhead() {
    if (this.check(TokenType.ELLIPSIS)) return true;
    if (
      this.check(
        TokenType.STR,
        TokenType.ID,
        TokenType.NUM,
        TokenType.FALSE,
        TokenType.TRUE
      )
    ) {
      const next = this.tokens[this.current + 1];
      return next && (next.type === TokenType.COLON || next.type === TokenType.LPAREN);
    }
    return false;
  }

  /**
   * Checks if the current token matches any of the provided types.
   * If a match is found, it consumes the token and returns true.
   * If no match is found, it returns false.
   * It also consumes any optional semicolon or newline before checking the current token.
   * @param {TokenType[]} types List of token types to match
   * @returns {boolean} True if a match is found, false otherwise
   */
  match(...types) {
    // this.consumeIfExist(TokenType.SEMICOLON, TokenType.NEWLINE);
    for (let type of types) {
      if (this.check(type)) {
        this.next();
        return true;
      }
    }
    return false;
  }

  check(type) {
    return !this.isAtEnd() && this.peek().type === type;
  }

  check(...types) {
    if (this.isAtEnd()) return false;
    for (let type of types) {
      if (this.peek().type === type) return true;
    }
    return false;
  }

  checkNext(type) {
    if (this.current + 1 >= this.tokens.length) return false;
    return this.tokens[this.current + 1].type === type;
  }

  isDelimiter(token = null) {
    // If token is provided, check if it's a delimiter
    if (token !== null) {
      return (
        token.type === TokenType.SEMICOLON || token.type === TokenType.NEWLINE
      );
    }

    // Original isDelimiter() behavior
    let res = false;
    while (true) {
      const type = this.peek().type;
      if (type === TokenType.SEMICOLON) {
        this.next();
        res = true;
      } else if (type === TokenType.RBRACE) {
        res = true;
        break;
      } else {
        break;
      }
    }
    return this.isAtEnd() || res;
  }

  needDelimiter() {
    // If there's no explicit semicolon or newline,
    // and it's not a line break,
    // and the next token is not a closing brace,
    // then we should insert a semicolon.
    if (!this.consumeIfExist(TokenType.SEMICOLON)) {
      if (!this.isLineBreak()) {
        if (!this.check(TokenType.RBRACE)) {
          return true;
        }
      }
    }
    // If we get here, we don't need a delimiter
    return false;
  }

  peek() {
    return this.tokens[this.current];
  }

  isAtEnd() {
    return this.peek().type === TokenType.EOF;
  }

  next() {
    if (!this.isAtEnd()) {
      this.current++;
      if (!this.isDelimiter(this.peek())) {
        this.last = this.peek();
      }
    }
    return this.previous();
  }

  previous() {
    return this.tokens[this.current - 1];
  }

  consume(type, message = null) {
    if (this.check(type)) {
      const token = this.next();
      // this.consumeIfExist(TokenType.SEMICOLON, TokenType.NEWLINE);
      return token;
    } else if (message != null) {
      throw new ParseError(message, this.peek().line, this.peek().column);
    } else {
      throw new ParseError();
    }
  }

  advance() {
    if (!this.isAtEnd()) {
      this.current++;
    }
  }

  consumeIfExist(...types) {
    let consumed = false;
    while (this.check(...types)) {
      this.advance();
      consumed = true;
    }
    return consumed;
  }

  isLineBreak() {
    // Compare line numbers of previous and current tokens
    return (
      this.previous().line < this.peek().line ||
      this.peek().type === TokenType.EOF
    );
  }

  pushLoop() {
    this.loopDepth++;
  }

  popLoop() {
    this.loopDepth--;
  }

  inLoop() {
    return this.loopDepth > 0;
  }
}
