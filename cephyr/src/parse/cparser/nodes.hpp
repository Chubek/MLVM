#pragma once
#include <ASTNode.h>

namespace cparser {
// Extensions retain C11 constructs that the upstream parser discards. Their
// evaluation belongs to Cephyr semantic analysis, not to parsing.
class StaticAssertASTNode : public ASTNode {
    ASTNode* expression;
    std::string message;
public:
    StaticAssertASTNode(ASTNode* expr, std::string message) : message(std::move(message)) {
        kind = NK_STATIC_ASSERT;
        ASSIGN_AST_NODE_REF(expression, expr);
    }
    ~StaticAssertASTNode() override { DELETE_AST_NODE_REF(expression); }
    ASTNode* getExpr() { return expression; }
    const std::string& getMessage() { return message; }
};
class GenericAssociationASTNode : public ASTNode {
    ASTNode *type, *expression;
public:
    GenericAssociationASTNode(ASTNode* type, ASTNode* expression) {
        kind = NK_GENERIC_ASSOC;
        ASSIGN_AST_NODE_REF(this->type, type);
        ASSIGN_AST_NODE_REF(this->expression, expression);
    }
    ~GenericAssociationASTNode() override { DELETE_AST_NODE_REF(type); DELETE_AST_NODE_REF(expression); }
    ASTNode* getType() { return type; }
    ASTNode* getExpr() { return expression; }
};
class GenericSelectionASTNode : public ASTNode {
    ASTNode *control, *associations;
public:
    GenericSelectionASTNode(ASTNode* control, ASTNode* associations) {
        kind = NK_GENERIC_SELECTION;
        ASSIGN_AST_NODE_REF(this->control, control);
        ASSIGN_AST_NODE_REF(this->associations, associations);
    }
    ~GenericSelectionASTNode() override { DELETE_AST_NODE_REF(control); DELETE_AST_NODE_REF(associations); }
    ASTNode* getControl() { return control; }
    ASTNode* getAssociations() { return associations; }
};
}
