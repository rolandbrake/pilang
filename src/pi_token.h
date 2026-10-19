#ifndef PI_TOKEN_H
#define PI_TOKEN_H

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Function to convert a void pointer to an integer
#define cast_int64(x) ((int64_t)x)

#define KW_NUM 30

/*
** Single-char tokens (terminal symbols) are represented by their own
** numeric code. Other tokens start at the following value.
*/
#define FIRST_RESERVED (UCHAR_MAX + 1)

// Enum for token types
typedef enum
{

    TK_FOR,
    TK_IN,
    TK_WHILE,
    TK_IF,
    TK_ELSE,
    TK_ELIF,
    TK_INF,
    TK_NAN,
    TK_BREAK,
    TK_CONTINUE,
    TK_GOTO,
    TK_FUN,
    TK_RETURN,
    TK_CLASS,
    TK_SUPER,
    TK_LET,
    TK_CONST,
    TK_TRUE,
    TK_FALSE,
    TK_NIL,
    TK_IS,
    TK_PRINT,
    TK_TYPEOF,
    TK_DEBUG,
    TK_SWITCH,
    TK_MATCH,
    TK_ID,
    TK_STR,
    TK_NUM,
    TK_BOOL,
    TK_LIST,
    TK_DIC,
    TK_SET,
    TK_LBRACKET,
    TK_RBRACKET,
    TK_LPAREN,
    TK_RPAREN,
    TK_LBRACE,
    TK_RBRACE,
    TK_SEMICOLON,
    TK_COLON,
    TK_COMMA,
    TK_ASSIGN,
    TK_DOT,
    TK_MINUS,
    TK_PLUS,
    TK_DIV,
    TK_MULT,
    TK_DOT_PROD, // Dot product operator [.*]
    TK_MOD,
    TK_BITOR,
    TK_BITAND,
    TK_XOR,
    TK_BITNEG,
    TK_EQUAL,
    TK_LESS,
    TK_GREATER,
    TK_NOT,
    TK_TICK,
    TK_DBQUOTE,
    TK_QUOTE,
    TK_QUESTION,
    TK_HASH,
    TK_LARROW,
    TK_RARROW,
    TK_PIPELINE,
    TK_DBDOTS,
    TK_INCR,
    TK_DECR,
    TK_POWER,
    TK_MINUS_ASSIGN,
    TK_PLUS_ASSIGN,
    TK_DIV_ASSIGN,
    TK_MULT_ASSIGN,
    TK_DOT_PROD_ASSIGN,
    TK_NEG_ASSIGN,
    TK_LESS_EQUAL,
    TK_BITAND_ASSIGN,
    TK_BITOR_ASSIGN,
    TK_XOR_ASSIGN,
    TK_MOD_ASSIGN,
    TK_NOT_EQUAL,
    TK_GREATER_EQUAL,
    TK_AND,
    TK_OR,
    TK_RSHIFT,
    TK_LSHIFT,
    TK_URSHIFT,
    TK_ELLIPSIS,
    TK_RSHIFT_ASSIGN,
    TK_LSHIFT_ASSIGN,
    TK_POWER_ASSIGN,
    TK_AND_ASSIGN,
    TK_OR_ASSIGN,
    TK_URSHIFT_ASSIGN,
    TK_IMPORT,
    TK_EOF,
    TK_INVALID,
} PiTokenType;

/* number of reserved words */
#define NUM_RESERVED (cast_int(TK_MATCH - FIRST_RESERVED + 1))

// Define the keyword structure
typedef struct
{
    char *name;   // key
    PiTokenType type; // value
} Keyword;

extern Keyword keywords[KW_NUM];

typedef struct
{
    PiTokenType type;
    char *start;
    int length;
    int line;
    int column;
    int open_at;
    int close_at;
    bool is_negative; // Flag indicating whether the current token is a negative number.
    bool skip; // Flag indicating whether the current token should be skipped.
} Token;

extern const char *token_names[];

Token create_token(PiTokenType type, char *start, int length, int line, int column);
PiTokenType token_type(Token token);
char *token_value(Token token);

int token_line(Token token);
int token_column(Token token);

const char *token_toString(Token token);

PiTokenType find_kw(const char *name);

double tk_double(const Token token);
char *tk_string(const Token token);
bool tk_bool(const Token token);

#endif
