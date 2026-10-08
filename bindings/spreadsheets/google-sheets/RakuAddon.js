// ---- the Marketplace add-on's own, in place of the attached script's (build.raku)
// The add-on's menu is under Extensions, and Google runs onInstall when it is
// installed, in the spreadsheet already open.
function onInstall(e) {
  onOpen(e);
}

function onOpen(e) {
  rakuMenu(SpreadsheetApp.getUi().createAddonMenu());
}
// ---- end of the add-on's own
