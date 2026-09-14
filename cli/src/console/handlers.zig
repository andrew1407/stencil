//! Console command implementations, one package per feature: each handler drives the same
//! pipeline.zig building blocks the flag mode uses, snapshots into the session's undo history,
//! and reports via ui.zig. Pure parsing lives in commands.zig. This file is the dispatch
//! surface console.zig binds to — the bodies live under handlers/.
const media = @import("handlers/media.zig");
const files = @import("handlers/files.zig");
const script = @import("handlers/script.zig");
const pageFormula = @import("handlers/pageFormula.zig");
const connections = @import("handlers/connections.zig");
const projects = @import("handlers/projects.zig");
const projectMeta = @import("handlers/projectMeta.zig");
const keywords = @import("handlers/keywords.zig");
const lifecycle = @import("handlers/lifecycle.zig");
const edit = @import("handlers/edit.zig");
const appearance = @import("handlers/appearance.zig");

// source in (handlers/media.zig)
pub const doUpload = media.doUpload;
pub const openProject = media.openProject;
pub const doSourceUpload = media.doSourceUpload;
pub const doBlank = media.doBlank;

// results out (handlers/files.zig)
pub const SaveTarget = files.SaveTarget;
pub const saveTarget = files.saveTarget;
pub const doSave = files.doSave;
pub const doLayout = files.doLayout;
pub const saveProject = files.saveProject;
pub const DeleteReject = files.DeleteReject;
pub const deleteReject = files.deleteReject;
pub const doDelete = files.doDelete;

// page + formulas (handlers/pageFormula.zig)
pub const printFormula = pageFormula.printFormula;
pub const doFormula = pageFormula.doFormula;
pub const doFormat = pageFormula.doFormat;

// server connections (handlers/connections.zig)
pub const doConnect = connections.doConnect;
pub const doDisconnect = connections.doDisconnect;
pub const doReconnect = connections.doReconnect;
pub const ConnFilter = connections.ConnFilter;
pub const parseConnFilter = connections.parseConnFilter;
pub const connFilterMatches = connections.connFilterMatches;
pub const doConnections = connections.doConnections;

// server projects (handlers/projects.zig, handlers/projectMeta.zig, handlers/keywords.zig)
pub const doProjects = projects.doProjects;
pub const doFetch = projects.doFetch;
pub const doProjectColor = projectMeta.doProjectColor;
pub const doProjectBlankColor = projectMeta.doProjectBlankColor;
pub const doProjectDescription = projectMeta.doProjectDescription;
pub const doKeywords = keywords.doKeywords;
pub const doKeywordsSearch = keywords.doKeywordsSearch;
pub const doKeywordsAdd = keywords.doKeywordsAdd;
pub const doKeywordsDel = keywords.doKeywordsDel;

// project lifecycle + session toggles (handlers/lifecycle.zig)
pub const doRename = lifecycle.doRename;
pub const doExpire = lifecycle.doExpire;
pub const doSync = lifecycle.doSync;
pub const doChat = lifecycle.doChat;

// transforms (handlers/edit.zig, handlers/script.zig)
pub const doScript = script.doScript;
pub const doScriptRun = script.doScriptRun;
pub const applyFilterArg = edit.applyFilterArg;
pub const doExec = edit.doExec;
pub const runAction = edit.runAction;

// history + look (handlers/appearance.zig)
pub const doStep = appearance.doStep;
pub const doReset = appearance.doReset;
pub const doDrop = appearance.doDrop;
pub const doTheme = appearance.doTheme;
pub const cycleTheme = appearance.cycleTheme;
pub const randomCustomTheme = appearance.randomCustomTheme;
pub const doMouse = appearance.doMouse;
pub const doRevealSpeed = appearance.doRevealSpeed;

test {
    _ = script;
    _ = media;
    _ = files;
    _ = pageFormula;
    _ = connections;
    _ = projects;
    _ = projectMeta;
    _ = @import("handlers/keywordTargets.zig");
    _ = keywords;
    _ = lifecycle;
    _ = edit;
    _ = appearance;
}
