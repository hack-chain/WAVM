#include <inttypes.h>
#include <stdlib.h>
#include <string>
#include <tuple>

#include "Lexer.h"
#include "WAVM/NFA/NFA.h"
#include "WAVM/RegExp/RegExp.h"

using namespace WAVM;
using namespace WAVM::WAST;

namespace WAVM {
    namespace WAST {
        struct LineInfo {
            U32 *lineStarts;
            U32 numLineStarts;
        };
    }
}

const char *WAST::describeToken(TokenType tokenType) {
    wavmAssert(tokenType < numTokenTypes);
    static const char *tokenDescriptions[] = {
#define VISIT_TOKEN(name, description, _) description,
            ENUM_TOKENS()
#undef VISIT_TOKEN
    };
    return tokenDescriptions[tokenType];
}

struct StaticData {
    NFA::Machine nfaMachine;

    StaticData();
};

static NFA::StateIndex createTokenSeparatorPeekState(NFA::Builder *builder, NFA::StateIndex finalState) {
    NFA::CharSet tokenSeparatorCharSet;
    tokenSeparatorCharSet.add(U8(' '));
    tokenSeparatorCharSet.add(U8('\t'));
    tokenSeparatorCharSet.add(U8('\r'));
    tokenSeparatorCharSet.add(U8('\n'));
    tokenSeparatorCharSet.add(U8('='));
    tokenSeparatorCharSet.add(U8('('));
    tokenSeparatorCharSet.add(U8(')'));
    tokenSeparatorCharSet.add(U8(';'));
    tokenSeparatorCharSet.add(0);
    auto separatorState = addState(builder);
    NFA::addEdge(builder, separatorState, tokenSeparatorCharSet, finalState | NFA::edgeDoesntConsumeInputFlag);
    return separatorState;
}

static void addLiteralToNFA(const char *string, NFA::Builder *builder, NFA::StateIndex initialState, NFA::StateIndex finalState) {
    for (const char *nextChar = string; *nextChar; ++nextChar) {
        NFA::StateIndex nextState = NFA::getNonTerminalEdge(builder, initialState, *nextChar);
        if (nextState < 0 || nextChar[1] == 0) {
            nextState = nextChar[1] == 0 ? finalState : addState(builder);
            NFA::addEdge(builder, initialState, NFA::CharSet(*nextChar), nextState);
        }
        initialState = nextState;
    }
}

StaticData::StaticData() {
    static const std::pair<TokenType, const char *> regexpTokenPairs[] = {{t_decimalInt,   "[+\\-]?\\d+(_\\d+)*"},
                                                                          {t_decimalFloat, "[+\\-]?\\d+(_\\d+)*\\.(\\d+(_\\d+)*)*([eE][+\\-]?\\d+(_\\d+)*)?"},
                                                                          {t_decimalFloat, "[+\\-]?\\d+(_\\d+)*[eE][+\\-]?\\d+(_\\d+)*"},

                                                                          {t_hexInt,       "[+\\-]?0[xX][\\da-fA-F]+(_[\\da-fA-F]+)*"},
                                                                          {t_hexFloat,     "[+\\-]?0[xX][\\da-fA-F]+(_[\\da-fA-F]+)*\\.([\\da-fA-F]+(_[\\da-fA-F]+)*)*([pP][+\\-]?\\d+(_\\d+)*)?"},
                                                                          {t_hexFloat,     "[+\\-]?0[xX][\\da-fA-F]+(_[\\da-fA-F]+)*[pP][+\\-]?\\d+(_\\d+)*"},

                                                                          {t_floatNaN,     "[+\\-]?nan(:0[xX][\\da-fA-F]+(_[\\da-fA-F]+)*)?"},
                                                                          {t_floatInf,     "[+\\-]?inf"},

                                                                          {t_string,       "\"([^\"\n\\\\]*(\\\\([^0-9a-fA-Fu]|[0-9a-fA-F][0-9a-fA-F]|u\\{[0-9a-fA-F]+})))*\""},

                                                                          {t_name,         "\\$[a-zA-Z0-9\'_+*/~=<>!?@#$%&|:`.\\-\\^\\\\]+"},
                                                                          {t_quotedName,   "\\$\"([^\"\n\\\\]*(\\\\([^0-9a-fA-Fu]|[0-9a-fA-F][0-9a-fA-F]|u\\{[0-9a-fA-F]+})))*\""},};
    static const std::tuple<TokenType, const char *, bool> literalTokenTuples[] = {std::make_tuple(t_leftParenthesis, "(", true), std::make_tuple(t_rightParenthesis, ")", true), std::make_tuple(t_equals, "=", true),

#define VISIT_TOKEN(name, _, literalString) std::make_tuple(t_##name, literalString, false),
                                                                                   ENUM_LITERAL_TOKENS()
#undef VISIT_TOKEN

#undef VISIT_OPERATOR_TOKEN
#define VISIT_OPERATOR_TOKEN(_, name, nameString, ...) std::make_tuple(t_##name, nameString, false),
                                                                                   ENUM_OPERATORS(VISIT_OPERATOR_TOKEN)
#undef VISIT_OPERATOR_TOKEN
    };

    NFA::Builder *nfaBuilder = NFA::createBuilder();

    for (auto regexpTokenPair : regexpTokenPairs) {
        NFA::StateIndex finalState = NFA::maximumTerminalStateIndex - (NFA::StateIndex) regexpTokenPair.first;
        finalState = createTokenSeparatorPeekState(nfaBuilder, finalState);
        RegExp::addToNFA(regexpTokenPair.second, nfaBuilder, 0, finalState);
    }

    for (auto literalTokenTuple : literalTokenTuples) {
        const TokenType tokenType = std::get<0>(literalTokenTuple);
        const char *literalString = std::get<1>(literalTokenTuple);
        const bool isTokenSeparator = std::get<2>(literalTokenTuple);

        NFA::StateIndex finalState = NFA::maximumTerminalStateIndex - (NFA::StateIndex) tokenType;
        if (!isTokenSeparator) {
            finalState = createTokenSeparatorPeekState(nfaBuilder, finalState);
        }

        addLiteralToNFA(literalString, nfaBuilder, 0, finalState);
    }

    nfaMachine = NFA::Machine(nfaBuilder);
}

inline bool isRecoveryPointChar(char c) {
    switch (c) {
        case ' ':
        case '\t':
        case '\r':
        case '\n':
        case '\f':
        case '(':
        case ')':
            return true;
        default:
            return false;
    };
}

Token *WAST::lex(const char *string, Uptr stringLength, LineInfo *&outLineInfo) {
    errorUnless(string);
    errorUnless(string[stringLength - 1] == 0);

    static StaticData staticData;

    if (stringLength > UINT32_MAX) {
        Errors::fatalf("cannot lex strings with more than %u characters", UINT32_MAX);
    }

    Token *tokens = (Token *) malloc(sizeof(Token) * (stringLength + 1));
    U32 *lineStarts = (U32 *) malloc(sizeof(U32) * (stringLength + 2));

    Token *nextToken = tokens;
    U32 *nextLineStart = lineStarts;
    *nextLineStart++ = 0;

    const char *nextChar = string;
    while (true) {
        while (true) {
            switch (*nextChar) {
                case ';':
                    if (nextChar[1] != ';') {
                        goto doneSkippingWhitespace;
                    } else {
                        nextChar += 2;
                        while (*nextChar) {
                            if (*nextChar == '\n') {
                                *nextLineStart++ = U32(nextChar - string + 1);
                                ++nextChar;
                                break;
                            }
                            ++nextChar;
                        };
                    }
                    break;
                case '(':
                    if (nextChar[1] != ';') {
                        goto doneSkippingWhitespace;
                    } else {
                        const char *firstCommentChar = nextChar;
                        nextChar += 2;
                        U32 commentDepth = 1;
                        while (commentDepth) {
                            if (nextChar[0] == ';' && nextChar[1] == ')') {
                                --commentDepth;
                                nextChar += 2;
                            } else if (nextChar[0] == '(' && nextChar[1] == ';') {
                                ++commentDepth;
                                nextChar += 2;
                            } else if (nextChar == string + stringLength - 1) {
                                nextToken->type = t_unterminatedComment;
                                nextToken->begin = U32(firstCommentChar - string);
                                ++nextToken;
                                goto doneSkippingWhitespace;
                            } else {
                                if (*nextChar == '\n') {
                                    *nextLineStart++ = U32(nextChar - string);
                                }
                                ++nextChar;
                            }
                        };
                    }
                    break;
                case '\n':
                    *nextLineStart++ = U32(nextChar - string + 1);
                    ++nextChar;
                    break;
                case ' ':
                case '\t':
                case '\r':
                case '\f':
                    ++nextChar;
                    break;
                default:
                    goto doneSkippingWhitespace;
            }
        }
        doneSkippingWhitespace:

        nextToken->begin = U32(nextChar - string);
        NFA::StateIndex terminalState = staticData.nfaMachine.feed(nextChar);
        if (terminalState != NFA::unmatchedCharacterTerminal) {
            nextToken->type = TokenType(NFA::maximumTerminalStateIndex - (NFA::StateIndex) terminalState);
            ++nextToken;
        } else {
            if (nextToken->begin < stringLength - 1) {
                nextToken->type = t_unrecognized;
                ++nextToken;

                const char *stringEnd = string + stringLength - 1;
                while (nextChar < stringEnd && !isRecoveryPointChar(*nextChar)) {
                    ++nextChar;
                }
            } else {
                break;
            }
        }
    }

    nextToken->type = t_eof;
    ++nextToken;

    *nextLineStart++ = U32(nextChar - string) + 1;

    const Uptr numLineStarts = nextLineStart - lineStarts;
    const Uptr numTokens = nextToken - tokens;
    lineStarts = (U32 *) realloc(lineStarts, sizeof(U32) * numLineStarts);
    tokens = (Token *) realloc(tokens, sizeof(Token) * numTokens);

    outLineInfo = new LineInfo{lineStarts, U32(numLineStarts)};

    return tokens;
}

void WAST::freeTokens(Token *tokens) {
    free(tokens);
}

void WAST::freeLineInfo(LineInfo *lineInfo) {
    free(lineInfo->lineStarts);
    delete lineInfo;
}

TextFileLocus WAST::calcLocusFromOffset(const char *string, const LineInfo *lineInfo, Uptr charOffset) {
    Uptr minLineIndex = 0;
    Uptr maxLineIndex = lineInfo->numLineStarts - 1;
    while (maxLineIndex > minLineIndex) {
        const Uptr medianLineIndex = (minLineIndex + maxLineIndex + 1) / 2;
        if (charOffset < lineInfo->lineStarts[medianLineIndex]) {
            maxLineIndex = medianLineIndex - 1;
        } else if (charOffset > lineInfo->lineStarts[medianLineIndex]) {
            minLineIndex = medianLineIndex;
        } else {
            minLineIndex = maxLineIndex = medianLineIndex;
        }
    };
    TextFileLocus result;
    result.newlines = (U32) minLineIndex;

    for (U32 index = lineInfo->lineStarts[result.newlines]; index < charOffset; ++index) {
        if (string[index] == '\t') {
            ++result.tabs;
        } else {
            ++result.characters;
        }
    }
    return result;
}
