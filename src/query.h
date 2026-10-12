#pragma once

// the reference app's search and Smart List language: free text with implicit AND, OR,
// -word / !word, word*, *word, "phrases", parentheses, and keyword filters
// (type:, tags:, taggroups:, locations:, lists:, groups:, modified:,
// created:, completed:, {datefilter}). One parsed Query is evaluated against
// many tasks.

#include <QDate>
#include <QDateTime>
#include <QString>
#include <functional>
#include <memory>

#include "model.h"
#include "schedule.h"

class Store;

struct QueryContext {
    const Store *store = nullptr;
    QDateTime now;
    ScheduleSettings settings;
    bool soundex = false;                          // Fuzzy "sounds like" word matching.
    QDate rangeFrom;                               // What {datefilter} stands for; invalid when unset.
    QDate rangeTo;
    std::function<bool(const Task &)> isPaused;    // May be empty.
};

class Query {
public:
    struct Node;

    Query();
    ~Query();
    Query(const Query &);
    Query &operator=(const Query &);

    // Never fails: unknown keywords and stray operators degrade to plain text,
    // so a half-typed search still filters sensibly. `error` describes what
    // was ignored, for an inline hint.
    static Query parse(const QString &text, QString *error = nullptr);

    bool isEmpty() const;
    bool matches(const Task &task, const QueryContext &context) const;

private:
    std::shared_ptr<const Node> m_root;
};
