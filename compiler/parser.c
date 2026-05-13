#include "parser.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

Parser* parser_create(const char *source) {
    Parser *p = malloc(sizeof(Parser));
    if (!p) return NULL;
    
    p->lexer = lexer_create(source);
    if (!p->lexer) {
        free(p);
        return NULL;
    }
    
    p->current = lexer_next(p->lexer);
    p->peek = lexer_next(p->lexer);
    
    return p;
}

void parser_destroy(Parser *p) {
    if (!p) return;
    if (p->lexer) lexer_destroy(p->lexer);
    free(p);
}

static void advance(Parser *p) {
    p->current = p->peek;
    p->peek = lexer_next(p->lexer);
}

static void skip_newlines(Parser *p) {
    while (p->current.type == TOK_NEWLINE) {
        advance(p);
    }
}

static int check(Parser *p, TokenType type) {
    return p->current.type == type;
}

static int match(Parser *p, TokenType type) {
    if (!check(p, type)) return 0;
    advance(p);
    return 1;
}

static Token expect(Parser *p, TokenType type, const char *msg) {
    if (p->current.type != type) {
        fprintf(stderr, "Parse error at line %d: %s (got %d)\n", p->current.line, msg, p->current.type);
    }
    Token tok = p->current;
    advance(p);
    return tok;
}

/* Forward declarations */
static ASTNode* parse_statement(Parser *p);
static ASTNode* parse_expression(Parser *p);

/* Parse program (list of declarations) */
static ASTNode* parse_program(Parser *p) {
    ASTNode *items = NULL;
    
    skip_newlines(p);
    
    while (p->current.type != TOK_EOF) {
        ASTNode *stmt = parse_statement(p);
        if (stmt) {
            items = ast_append(items, stmt);
        }
        skip_newlines(p);
    }
    
    return ast_program(items);
}

/* Parse a block of statements */
static ASTNode* parse_block(Parser *p) {
    ASTNode *items = NULL;
    
    while (p->current.type != TOK_EOF && p->current.type != TOK_END 
           && p->current.type != TOK_ENDIF && p->current.type != TOK_ELSE) {
        skip_newlines(p);
        
        if (p->current.type == TOK_END || p->current.type == TOK_ENDIF || p->current.type == TOK_ELSE) {
            break;
        }
        
        ASTNode *stmt = parse_statement(p);
        if (stmt) {
            items = ast_append(items, stmt);
        }
        
        skip_newlines(p);
    }
    
    return items ? items : ast_block(NULL);
}

/* Parse primary expression */
static ASTNode* parse_primary(Parser *p) {
    skip_newlines(p);
    
    if (p->current.type == TOK_NUMBER) {
        int64_t val = atoll(p->current.value);
        advance(p);
        return ast_number(val);
    }
    
    if (p->current.type == TOK_STRING) {
        char *str = (char*)p->current.value;
        advance(p);
        return ast_string(str);
    }
    
    if (p->current.type == TOK_IDENT) {
        char *name = (char*)p->current.value;
        advance(p);
        
        if (match(p, TOK_LPAREN)) {
            /* Function call */
            ASTNode *args = NULL;
            if (!check(p, TOK_RPAREN)) {
                args = parse_expression(p);
                while (match(p, TOK_COMMA)) {
                    ASTNode *arg = parse_expression(p);
                    args = ast_append(args, arg);
                }
            }
            expect(p, TOK_RPAREN, "Expected ')'");
            return ast_call(name, args);
        }
        
        return ast_ident(name);
    }
    
    if (match(p, TOK_LPAREN)) {
        ASTNode *expr = parse_expression(p);
        expect(p, TOK_RPAREN, "Expected ')'");
        return expr;
    }
    
    if (match(p, TOK_LBRACKET)) {
        ASTNode *items = NULL;
        if (!check(p, TOK_RBRACKET)) {
            items = parse_expression(p);
            while (match(p, TOK_COMMA)) {
                ASTNode *item = parse_expression(p);
                items = ast_append(items, item);
            }
        }
        expect(p, TOK_RBRACKET, "Expected ']'");
        return ast_array(items);
    }
    
    fprintf(stderr, "Parse error: unexpected token %d at line %d\n", p->current.type, p->current.line);
    return NULL;
}

/* Parse unary expression */
static ASTNode* parse_unary(Parser *p) {
    if (match(p, TOK_NOT)) {
        return ast_unop('!', parse_unary(p));
    }
    if (match(p, TOK_MINUS)) {
        return ast_unop('-', parse_unary(p));
    }
    if (match(p, TOK_BITNOT)) {
        return ast_unop('~', parse_unary(p));
    }
    
    return parse_primary(p);
}

/* Parse multiplicative expression */
static ASTNode* parse_mult(Parser *p) {
    ASTNode *left = parse_unary(p);
    
    while (p->current.type == TOK_STAR || p->current.type == TOK_SLASH || p->current.type == TOK_PERCENT) {
        char op = p->current.value[0];
        advance(p);
        ASTNode *right = parse_unary(p);
        left = ast_binop(op, left, right);
    }
    
    return left;
}

/* Parse additive expression */
static ASTNode* parse_add(Parser *p) {
    ASTNode *left = parse_mult(p);
    
    while (p->current.type == TOK_PLUS || p->current.type == TOK_MINUS) {
        char op = p->current.value[0];
        advance(p);
        ASTNode *right = parse_mult(p);
        left = ast_binop(op, left, right);
    }
    
    return left;
}

/* Parse comparison expression */
static ASTNode* parse_comparison(Parser *p) {
    ASTNode *left = parse_add(p);
    
    while (p->current.type == TOK_LT || p->current.type == TOK_GT 
           || p->current.type == TOK_LE || p->current.type == TOK_GE
           || p->current.type == TOK_EQ || p->current.type == TOK_NE) {
        TokenType op_type = p->current.type;
        advance(p);
        ASTNode *right = parse_add(p);
        
        char op = 0;
        if (op_type == TOK_LT) op = '<';
        else if (op_type == TOK_GT) op = '>';
        else if (op_type == TOK_EQ) op = '=';
        else if (op_type == TOK_NE) op = '!';
        
        left = ast_binop(op, left, right);
    }
    
    return left;
}

/* Parse logical AND */
static ASTNode* parse_and(Parser *p) {
    ASTNode *left = parse_comparison(p);
    
    while (p->current.type == TOK_AND) {
        advance(p);
        ASTNode *right = parse_comparison(p);
        left = ast_binop('&', left, right);
    }
    
    return left;
}

/* Parse logical OR */
static ASTNode* parse_or(Parser *p) {
    ASTNode *left = parse_and(p);
    
    while (p->current.type == TOK_OR) {
        advance(p);
        ASTNode *right = parse_and(p);
        left = ast_binop('|', left, right);
    }
    
    return left;
}

/* Parse expression */
static ASTNode* parse_expression(Parser *p) {
    return parse_or(p);
}

/* Parse IF statement */
static ASTNode* parse_if_stmt(Parser *p) {
    expect(p, TOK_IF, "Expected 'IF'");
    skip_newlines(p);
    
    ASTNode *cond = parse_expression(p);
    skip_newlines(p);
    expect(p, TOK_COLON, "Expected ':' after IF condition");
    skip_newlines(p);
    
    ASTNode *then_br = parse_block(p);
    skip_newlines(p);
    
    ASTNode *else_br = NULL;
    if (match(p, TOK_ELSE)) {
        skip_newlines(p);
        expect(p, TOK_COLON, "Expected ':' after ELSE");
        skip_newlines(p);
        else_br = parse_block(p);
        skip_newlines(p);
    }
    
    expect(p, TOK_ENDIF, "Expected 'ENDIF'");
    
    return ast_if(cond, then_br, else_br);
}

/* Parse WHILE statement */
static ASTNode* parse_while_stmt(Parser *p) {
    expect(p, TOK_WHILE, "Expected 'WHILE'");
    skip_newlines(p);
    
    ASTNode *cond = parse_expression(p);
    skip_newlines(p);
    expect(p, TOK_COLON, "Expected ':' after WHILE condition");
    skip_newlines(p);
    
    ASTNode *body = parse_block(p);
    skip_newlines(p);
    expect(p, TOK_END, "Expected 'END'");
    
    return ast_while(cond, body);
}

/* Parse statement */
static ASTNode* parse_statement(Parser *p) {
    skip_newlines(p);
    
    if (match(p, TOK_SIGIL)) {
        Token name = expect(p, TOK_STRING, "Expected sigil name");
        expect(p, TOK_COLON, "Expected ':' after sigil");
        skip_newlines(p);
        ASTNode *body = parse_block(p);
        return ast_sigil((const char*)name.value, body);
    }
    
    if (match(p, TOK_PERSONA)) {
        Token name = expect(p, TOK_STRING, "Expected persona name");
        expect(p, TOK_COLON, "Expected ':' after persona");
        skip_newlines(p);
        ASTNode *body = parse_block(p);
        expect(p, TOK_END, "Expected 'END PERSONA'");
        return ast_persona((const char*)name.value, body);
    }
    
    if (match(p, TOK_RITUAL)) {
        Token name = expect(p, TOK_STRING, "Expected ritual name");
        expect(p, TOK_COLON, "Expected ':' after ritual");
        skip_newlines(p);
        ASTNode *body = parse_block(p);
        expect(p, TOK_END, "Expected 'END RITUAL'");
        return ast_ritual((const char*)name.value, body);
    }
    
    if (match(p, TOK_SAY)) {
        ASTNode *expr = parse_expression(p);
        return ast_say(expr);
    }
    
    if (match(p, TOK_FEEL)) {
        uint32_t intensity = 50;
        if (check(p, TOK_NUMBER)) {
            intensity = atoi(p->current.value);
            advance(p);
        }
        return ast_feel(intensity);
    }
    
    if (match(p, TOK_PERCEIVE)) {
        ASTNode *expr = parse_expression(p);
        if (match(p, TOK_ARROW)) {
            expect(p, TOK_IDENT, "Expected variable name after PERCEIVE");
            return ast_perceive(expr, (const char*)p->current.value);
        }
        return expr;
    }
    
    if (match(p, TOK_IF)) {
        return parse_if_stmt(p);
    }
    
    if (match(p, TOK_WHILE)) {
        return parse_while_stmt(p);
    }
    
    if (match(p, TOK_RETURN)) {
        ASTNode *expr = NULL;
        if (!check(p, TOK_NEWLINE) && !check(p, TOK_EOF)) {
            expr = parse_expression(p);
        }
        return ast_return(expr);
    }
    
    if (check(p, TOK_IDENT)) {
        Token ident = p->current;
        advance(p);
        
        if (match(p, TOK_ASSIGN)) {
            ASTNode *expr = parse_expression(p);
            return ast_assign((const char*)ident.value, expr);
        } else {
            return ast_ident((const char*)ident.value);
        }
    }
    
    return NULL;
}

ASTNode* parser_parse(Parser *p) {
    return parse_program(p);
}
