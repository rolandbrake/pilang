#ifndef PI_PARSER_H
#define PI_PARSER_H

#include <stdbool.h>
#include "pi_token.h"
#include "pi_compiler.h"


// Defines the parsing mode, which can affect how source code is interpreted.
typedef enum
{
    MODE_FILE, // Default mode for parsing full script files.
    MODE_REPL  // Mode for parsing single expressions in a REPL environment.
} ParserMode;

/**
 * Parser state structure.
 */
typedef struct
{
    Token *tokens; // Array of tokens being parsed.
    Token last;    // Last token parsed.
    int current;     // Current token index being parsed.
    bool access;     // Flag indicating whether access to variables is allowed.
    bool is_store;   // Flag indicating whether a store operation is in progress.
    bool force_store; // Force the current access chain to compile as a store.
    bool emit_load;  // Flag indicating whether to emit a LOAD instruction.
    bool is_assign;  // Flag indicating whether an assignment is in progress.
    bool has_walrus; // Flag indicating whether a walrus operator is in progress.
    bool object_member; // True only while parsing a direct object-literal value expression.
    PiTokenType op;      // Type of operator being parsed (e.g. TK_PLUS, TK_MINUS, etc.).

    // Compiler associated with the parser
    Compiler *comp;

    bool is_return;
    char *fun_name; // (pending functoin name) Inferred name for the next direct function literal RHS.
    char *object_name; // Inferred name for the next direct object-literal RHS.

    HT_Table *instance_field_names;
    List *instance_fields;

    ParserMode mode; // The current parsing mode (file or REPL).

    bool had_error; // Flag indicating if an error has occurred during parsing.

} Parser;

/**
 * Assignment operation structure.
 */
typedef struct
{
    int left;   // Index of the left-hand side of the assignment.
    int right;  // Index of the right-hand side of the assignment.
    PiTokenType op; // Type of the assignment operator (e.g. TK_ASSIGN, TK_PLUS_EQUAL, etc.).

} Assignment;

Parser *init_parser(Compiler *comp, Token *tokens, ParserMode mode);
void parse(Parser *parser);
void free_parser(Parser *parser);

#endif
