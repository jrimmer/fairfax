#pragma once

// The rule builder's model: a tree of "all/any of the following" groups and
// Property / operator / value clauses, converted to and from the text of a
// `type:` section of the search language (see query.cpp for the grammar).
//
// A group is {match: "all"|"any", items: [...]}; a clause is
// {prop: "<key>", neg: bool, value: "<token>"}. Both are QVariantMaps so they
// travel to QML as plain objects.

#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace SearchRules {

// Parses the text after `type:`. On anything the builder cannot show
// (unknown tokens, alltasks, capturedmail, a negated parenthesis, quotes,
// unbalanced parentheses) it returns {} and sets *ok to false.
QVariantMap fromType(const QString &typeValue, bool *ok = nullptr);

// The value for a `type:` section, without the "type: " prefix; "" for a tree
// with no clauses.
QString toType(const QVariantMap &tree);

// The property table for the QML rows: key, title, ops [positive, negative],
// values [{token, label}] and single (true when there is only one value).
QVariantList properties();

}  // namespace SearchRules
