#pragma once

// Each test file registers its QObject test class here with REGISTER_TEST, and
// main.cpp runs them all, so adding a test file never means editing main.

#include <QList>
#include <QObject>
#include <functional>

using TestFactory = std::function<QObject *()>;
QList<TestFactory> &testRegistry();

#define REGISTER_TEST(Class) \
    static const bool registered_##Class = (testRegistry().append([] { return new Class; }), true);
