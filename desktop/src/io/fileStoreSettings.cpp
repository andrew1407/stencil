#include "fileStore.hpp"
#include "fileStoreIo.hpp"
#include "deferredWrite.hpp"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QStandardPaths>

namespace stencil::gui {

  QString fileStore::stateDir() {
    const QString dir = baseDir();
    QDir().mkpath(dir);
    return dir;
  }
  QString fileStore::settingsPath() { return stateDir() + "/settings.json"; }
  QString fileStore::sessionPath() { return stateDir() + "/session.autosave"; }
  QString fileStore::projectsPath() { return stateDir() + "/projects.json"; }
  QString fileStore::secretsPath() { return stateDir() + "/secrets.json"; }
  QJsonObject fileStore::loadSecrets() { return readJson(secretsPath()).object(); }
  void fileStore::saveSecrets(const QJsonObject& o) {
    writeJson(secretsPath(), QJsonDocument(o), /*ownerOnly=*/true);
  }

  Settings fileStore::loadSettings() {
    Settings s;
    // Seed the locale-based default before reading the file, so a brand-new user or an older config
    // without a "units" key still gets one; a stored "units" value below overrides it.
    s.units = localeDefaultUnit();
    const QJsonObject o = readJson(settingsPath()).object();
    if (o.isEmpty()) return s;
    return settingsFromJson(o, s);
  }

  Settings fileStore::settingsFromJson(const QJsonObject& o, const Settings& base) {
    Settings s = base;
    // themeMode is the new key; migrate the legacy `theme` ("dark"->dark,
    // "light"->light, anything else / missing -> system).
    if (o.contains("themeMode")) {
      s.themeMode = o.value("themeMode").toString(s.themeMode);
    } else if (o.contains("theme")) {
      const QString legacy = o.value("theme").toString();
      s.themeMode = (legacy == "dark" || legacy == "light") ? legacy : "system";
    }
    s.accentColor = o.value("accentColor").toString(s.accentColor);
    s.autosave = o.value("autosave").toBool(s.autosave);
    s.syncToServer = o.value("syncToServer").toBool(s.syncToServer);
    s.showPoints = o.value("showPoints").toBool(s.showPoints);
    s.showLines = o.value("showLines").toBool(s.showLines);
    s.defaultColor = o.value("defaultColor").toString(s.defaultColor);
    s.defaultPointColor = o.value("defaultPointColor").toString(s.defaultPointColor);
    s.defaultThickness = o.value("defaultThickness").toDouble(s.defaultThickness);
    s.defaultPointSize = o.value("defaultPointSize").toDouble(s.defaultPointSize);
    s.defaultStyle = o.value("defaultStyle").toString(s.defaultStyle);
    s.defaultFillColor = o.value("defaultFillColor").toString(s.defaultFillColor);
    s.selGlowColor = o.value("selGlowColor").toString(s.selGlowColor);
    s.hoverRingColor = o.value("hoverRingColor").toString(s.hoverRingColor);
    s.focusRingColor = o.value("focusRingColor").toString(s.focusRingColor);
    s.pageSize = o.value("pageSize").toString(s.pageSize);
    s.customPageWidth = o.value("customPageWidth").toDouble(s.customPageWidth);
    s.customPageHeight = o.value("customPageHeight").toDouble(s.customPageHeight);
    s.units = o.value("units").toString(s.units);
    s.allowFormulas = o.value("allowFormulas").toBool(s.allowFormulas);
    s.formulaX = o.value("formulaX").toString(s.formulaX);
    s.formulaY = o.value("formulaY").toString(s.formulaY);
    s.tooltipEnabled = o.value("tooltipEnabled").toBool(s.tooltipEnabled);
    s.tooltipShowPage = o.value("tooltipShowPage").toBool(s.tooltipShowPage);
    s.tooltipShowScreen = o.value("tooltipShowScreen").toBool(s.tooltipShowScreen);
    s.tooltipShowCoords = o.value("tooltipShowCoords").toBool(s.tooltipShowCoords);
    // Image filter + custom tint (browser storage.js:309-311).
    s.imageFilter = o.value("imageFilter").toString(s.imageFilter);
    s.filterColor = o.value("filterColor").toString(s.filterColor);
    s.holdDrawDelay = o.value("holdDrawDelay").toInt(s.holdDrawDelay);
    s.drawingAnimations = o.value("drawingAnimations").toBool(s.drawingAnimations);
    s.modalBackdrop = o.value("modalBackdrop").toBool(s.modalBackdrop);
    s.motionMode = o.value("motionMode").toString(s.motionMode);
    s.notifyChannel = o.value("notifyChannel").toString(s.notifyChannel);
    s.browserBaseUrl = o.value("browserBaseUrl").toString(s.browserBaseUrl);
    s.telegramBotUsername = o.value("telegramBotUsername").toString(s.telegramBotUsername);
    // AI assistant (llm-contract.md §5 persistence keys) + the saved dock state.
    s.llmProvider = o.value("llmProvider").toString(s.llmProvider);
    s.llmBaseUrl = o.value("llmBaseUrl").toString(s.llmBaseUrl);
    s.llmModel = o.value("llmModel").toString(s.llmModel);
    s.llmApiKey = o.value("llmApiKey").toString(s.llmApiKey);
    s.llmServerUrl = o.value("llmServerUrl").toString(s.llmServerUrl);
    s.saveChatsWithProject = o.value("saveChatsWithProject").toBool(s.saveChatsWithProject);
    s.chatSwapSides = o.value("chatSwapSides").toBool(s.chatSwapSides);
    s.nativeMenuBar = o.value("nativeMenuBar").toBool(s.nativeMenuBar);
    s.windowState = o.value("windowState").toString(s.windowState);
    return s;
  }

  void fileStore::saveSettings(const Settings& s) {
    // Owner-only: this file holds llmApiKey in plaintext (contract §5).
    writeJson(settingsPath(), QJsonDocument(settingsToJson(s)), /*ownerOnly=*/true);
  }

  QJsonObject fileStore::settingsToJson(const Settings& s) {
    QJsonObject o;
    o["themeMode"] = s.themeMode;
    o["accentColor"] = s.accentColor;
    o["autosave"] = s.autosave;
    o["syncToServer"] = s.syncToServer;
    o["showPoints"] = s.showPoints;
    o["showLines"] = s.showLines;
    o["defaultColor"] = s.defaultColor;
    o["defaultPointColor"] = s.defaultPointColor;
    o["defaultThickness"] = s.defaultThickness;
    o["defaultPointSize"] = s.defaultPointSize;
    o["defaultStyle"] = s.defaultStyle;
    o["defaultFillColor"] = s.defaultFillColor;
    o["selGlowColor"] = s.selGlowColor;
    o["hoverRingColor"] = s.hoverRingColor;
    o["focusRingColor"] = s.focusRingColor;
    o["pageSize"] = s.pageSize;
    o["customPageWidth"] = s.customPageWidth;
    o["customPageHeight"] = s.customPageHeight;
    o["units"] = s.units;
    o["allowFormulas"] = s.allowFormulas;
    o["formulaX"] = s.formulaX;
    o["formulaY"] = s.formulaY;
    o["tooltipEnabled"] = s.tooltipEnabled;
    o["tooltipShowPage"] = s.tooltipShowPage;
    o["tooltipShowScreen"] = s.tooltipShowScreen;
    o["tooltipShowCoords"] = s.tooltipShowCoords;
    o["imageFilter"] = s.imageFilter;
    o["filterColor"] = s.filterColor;
    o["holdDrawDelay"] = s.holdDrawDelay;
    o["drawingAnimations"] = s.drawingAnimations;
    o["modalBackdrop"] = s.modalBackdrop;
    o["motionMode"] = s.motionMode;
    o["notifyChannel"] = s.notifyChannel;
    o["browserBaseUrl"] = s.browserBaseUrl;
    o["telegramBotUsername"] = s.telegramBotUsername;
    o["llmProvider"] = s.llmProvider;
    o["llmBaseUrl"] = s.llmBaseUrl;
    o["llmModel"] = s.llmModel;
    o["llmApiKey"] = s.llmApiKey;
    o["llmServerUrl"] = s.llmServerUrl;
    o["saveChatsWithProject"] = s.saveChatsWithProject;
    o["chatSwapSides"] = s.chatSwapSides;
    o["nativeMenuBar"] = s.nativeMenuBar;
    o["windowState"] = s.windowState;
    return o;
  }
}

