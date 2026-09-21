#pragma once
// Shared ground for the persistence-corpus headless TUs: the two fixture readers every walker uses,
// and the walkers themselves. Each returns how many local overrides it applied.
#include "fileStore.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

#include "../support/check.hpp"
#include "../support/fixtureCorpus.hpp"

using namespace stencil::gui;
namespace core = stencil::core;

QJsonArray loadCases(const char* rel);
QString taggedName(const QString& name, const FixtureOverride& ov);

int walkChatDoc();
int walkLayout();
int walkStencilProject(const char* rel);
