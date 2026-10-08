// ---- the Marketplace add-on's own, in place of the attached script's (build.raku)
// The add-on's menu is under Extensions, and Google runs onInstall when it is
// installed, in the spreadsheet already open. What formulas print is not
// kept: an add-on's log is its developer's, not the spreadsheet owner's.
function onInstall(e) {
  onOpen(e);
}

function onOpen(e) {
  rakuMenu(SpreadsheetApp.getUi().createAddonMenu());
}

function rakuPrinted(lines) {
}
// ---- end of the add-on's own
