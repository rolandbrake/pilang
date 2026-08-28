
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include "pi_token.h"

keyword_t keywords[KW_NUM] = {
    {"false", TK_FALSE},
    {"true", TK_TRUE},
    {"for", TK_FOR},
    {"in", TK_IN},
    {"while", TK_WHILE},
    {"fun", TK_FUN},
    {"let", TK_LET},
    {"const", TK_CONST},
    {"INF", TK_INF},
    {"NAN", TK_NAN},
    {"break", TK_BREAK},
    {"continue", TK_CONTINUE},
    {"goto", TK_GOTO},
    {"if", TK_IF},
    {"else", TK_ELSE},
    {"elif", TK_ELIF},
    {"nil", TK_NIL},
    {"is", TK_IS},
    {"return", TK_RETURN},
    {"class", TK_CLASS},
    {"super", TK_SUPER},
    {"typeof", TK_TYPEOF},
    {"debug", TK_DEBUG},
    {"import", TK_IMPORT},
    {"switch", TK_SWITCH},
    {"match", TK_MATCH},
};

const char *token_names[] = {
    "TK_FOR",
    "TK_IN",
    "TK_WHILE",
    "TK_IF",
    "TK_ELSE",
    "TK_ELIF",
    "TK_INF",
    "TK_NAN",
    "TK_BREAK",
    "TK_CONTINUE",
    "TK_GOTO",
    "TK_FUN",
    "TK_RETURN",
    "TK_CLASS",
    "TK_SUPER",
    "TK_LET",
    "TK_CONST",
    "TK_TRUE",
    "TK_FALSE",
    "TK_NIL",
    "TK_IS",
    "TK_PRINT",
    "TK_TYPEOF",
    "TK_DEBUG",
    "TK_SWITCH",
    "TK_MATCH",
    "TK_ID",
    "TK_STR",
    "TK_NUM",
    "TK_BOOL",
    "TK_LIST",
    "TK_DIC",
    "TK_SET",
    "TK_LBRACKET",
    "TK_RBRACKET",
    "TK_LPAREN",
    "TK_RPAREN",
    "TK_LBRACE",
    "TK_RBRACE",
    "TK_SEMICOLON",
    "TK_COLON",
    "TK_COMMA",
    "TK_ASSIGN",
    "TK_DOT",
    "TK_MINUS",
    "TK_PLUS",
    "TK_DIV",
    "TK_MULT",
    "TK_DOT_PROD",
    "TK_MOD",
    "TK_BITOR",
    "TK_BITAND",
    "TK_XOR",
    "TK_BITNEG",
    "TK_EQUAL",
    "TK_LESS",
    "TK_GREATER",
    "TK_NOT",
    "TK_TICK",
    "TK_DBQUOTE",
    "TK_QUOTE",
    "TK_QUESTION",
    "TK_HASH",
    "TK_LARROW",
    "TK_RARROW",
    "TK_PIPELINE",
    "TK_DBDOTS",
    "TK_INCR",
    "TK_DECR",
    "TK_POWER",
    "TK_MINUS_ASSIGN",
    "TK_PLUS_ASSIGN",
    "TK_DIV_ASSIGN",
    "TK_MULT_ASSIGN",
    "TK_DOT_PROD_ASSIGN",
    "TK_NEG_ASSIGN",
    "TK_LESS_EQUAL",
    "TK_NOT_EQUAL",
    "TK_GREATER_EQUAL",
    "TK_BITAND_ASSIGN",
    "TK_BITOR_ASSIGN",
    "TK_XOR_ASSIGN",
    "TK_MOD_ASSIGN",
    "TK_AND",
    "TK_OR",
    "TK_RSHIFT",
    "TK_LSHIFT",
    "TK_URSHIFT",
    "TK_ELLIPSIS",
    "TK_RSHIFT_ASSIGN",
    "TK_LSHIFT_ASSIGN",
    "TK_POWER_ASSIGN",
    "TK_AND_ASSIGN",
    "TK_OR_ASSIGN",
    "TK_URSHIFT_ASSIGN",
    "TK_IMPORT",
    "TK_EOF",
};

token_t create_token(tk_type type, char *start, int length, int line, int column)
{
    token_t token;

    // Allocate memory for the token's lexeme and copy it
    token.start = (char *)malloc(length + 1);
    if (!token.start)
    {
        perror("Failed to allocate memory for token lexeme");
        exit(EXIT_FAILURE);
    }

    memcpy(token.start, start, length);
    token.start[length] = '\0'; // Ensure null termination

    // Set the other token properties
    token.type = type;
    token.length = length;
    token.line = line;
    token.column = column;
    token.is_negative = false;
    token.skip = false;

    return token;
}

tk_type token_type(token_t token)
{
    return token.type;
}

// Function to get the value of the token as a string
char *token_value(token_t token)
{
    int length = token.length;
    char *start = token.start;

    // Check if the token is negative and adjust memory allocation
    int _length = token.is_negative ? 1 : 0;      // Add 1 for '-' if negative
    char *new_str = malloc(length + _length + 1); // +1 for null terminator

    if (new_str)
    {
        // If negative, add the minus sign to the beginning
        if (token.is_negative)
        {
            new_str[0] = '-';
            strncpy(new_str + 1, start, length); // Copy the original token after '-'
        }
        else
            strncpy(new_str, start, length); // Copy the original token
        new_str[length + _length] = '\0';    // Set null terminator
    }

    return new_str;
}

int token_line(token_t token)
{
    return token.line;
}

int token_column(token_t token)
{
    return token.column;
}

// Function to convert token to string (simple implementation)
const char *token_toString(token_t token)
{
    // Allocate enough space to hold the type, token value, and additional characters
    // Adjust the buffer size as needed to fit your largest expected token value
    int buffer_size = 256;
    char *result = (char *)malloc(buffer_size);
    if (token.length == 0)
        snprintf(result, buffer_size, "<%s>", token_names[token.type]);
    else
        snprintf(result, buffer_size, "<%s, %.*s>", token_names[token.type], token.length, token.start);

    return result;
}

tk_type find_kw(const char *name)
{
    for (int i = 0; i < KW_NUM; ++i)
        if (strcmp(keywords[i].name, name) == 0)
            return keywords[i].type;
    return TK_INVALID;
}

double tk_double(const token_t token)
{
    // Numeric separators are syntax only; omit them before conversion.
    char *buffer = malloc(token.length + 1);
    if (!buffer)
    {
        perror("Failed to allocate numeric token buffer");
        exit(EXIT_FAILURE);
    }

    int length = 0;
    for (int i = 0; i < token.length; i++)
        if (token.start[i] != '_')
            buffer[length++] = token.start[i];
    buffer[length] = '\0';

    // Convert the string to a double.
    double result = strtod(buffer, NULL);

    // Clean up the temporary buffer.
    free(buffer);

    return token.is_negative ? -result : result;
}

char *tk_string(const token_t token)
{
    // char *buffer = (char *)malloc(token.length + 1);
    // snprintf(buffer, token.length + 1, "%.*s", token.length, token.start);
    // return buffer; // The caller is responsible for freeing this memory
    return token_value(token);
}

bool tk_bool(const token_t token)
{
    char *str = token_value(token);
    if (strcmp(str, "true") == 0)
        return true;
    else
        return false;
}
