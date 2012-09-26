/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

netscape.security.PrivilegeManager.enablePrivilege("UniversalXPConnect");

const {classes: Cc, interfaces: Ci, utils: Cu} = Components;

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/devtools/dbg-server.jsm");
Cu.import("resource://gre/modules/devtools/dbg-client.jsm");
Cu.import("resource://gre/modules/devtools/WebConsoleUtils.jsm");

function initCommon()
{
  // Always log packets when running tests.
  Services.prefs.setBoolPref("devtools.debugger.log", true);
}

function initDebuggerServer()
{
  if (!DebuggerServer.initialized) {
    DebuggerServer.init();
    DebuggerServer.addBrowserActors();
  }
}

function connectToDebugger(aCallback)
{
  initCommon();
  initDebuggerServer();

  let transport = DebuggerServer.connectPipe();
  let client = new DebuggerClient(transport);

  let dbgState = { dbgClient: client };
  client.connect(aCallback.bind(null, dbgState));
}

function attachConsole(aListeners, aCallback)
{
  function _onAttachConsole(aState, aResponse, aWebConsoleClient)
  {
    if (aResponse.error) {
      Cu.reportError("attachConsole failed: " + aResponse.error + " " +
                     aResponse.message);
    }

    aState.client = aWebConsoleClient;

    aCallback(aState, aResponse);
  }

  connectToDebugger(function _onConnect(aState) {
    aState.dbgClient.listTabs(function _onListTabs(aResponse) {
      if (aResponse.error) {
        Cu.reportError("listTabs failed: " + aResponse.error + " " +
                       aResponse.message);
        aCallback(aState, aResponse);
        return;
      }
      let tab = aResponse.tabs[aResponse.selected];
      aState.actor = tab.consoleActor;
      aState.dbgClient.attachConsole(tab.consoleActor, aListeners,
                                     _onAttachConsole.bind(null, aState));
    });
  });
}

function closeDebugger(aState, aCallback)
{
  aState.dbgClient.close(aCallback);
  aState.dbgClient = null;
  aState.client = null;
}

function checkConsoleAPICall(aCall, aExpected)
{
  if (aExpected.level != "trace" && aExpected.arguments) {
    is(aCall.arguments.length, aExpected.arguments.length,
       "number of arguments");
  }

  checkObject(aCall, aExpected);
}

function checkObject(aObject, aExpected)
{
  for (let name of Object.keys(aExpected))
  {
    let expected = aExpected[name];
    let value = aObject[name];
    if (value === undefined) {
      ok(false, "'" + name + "' is undefined");
    }
    else if (typeof expected == "string" ||
        typeof expected == "number" ||
        typeof expected == "boolean") {
      is(value, expected, "property '" + name + "'");
    }
    else if (expected instanceof RegExp) {
      ok(expected.test(value), name + ": " + expected);
    }
    else if (Array.isArray(expected)) {
      info("checking array for property '" + name + "'");
      checkObject(value, expected);
    }
    else if (typeof expected == "object") {
      info("checking object for property '" + name + "'");
      checkObject(value, expected);
    }
  }
}

function checkHeadersOrCookies(aArray, aExpected)
{
  for (let elem of aArray) {
    if (!(elem.name in aExpected)) {
      continue;
    }
    let expected = aExpected[elem.name];
    if (expected instanceof RegExp) {
      ok(expected.test(elem.value), elem.name + ": " + expected);
    }
    else {
      is(elem.value, expected, elem.name);
    }
  }
}
