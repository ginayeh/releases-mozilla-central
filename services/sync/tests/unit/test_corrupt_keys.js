/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

Cu.import("resource://services-common/log4moz.js");
Cu.import("resource://services-sync/constants.js");
Cu.import("resource://services-sync/engines.js");
Cu.import("resource://services-sync/engines/tabs.js");
Cu.import("resource://services-sync/engines/history.js");
Cu.import("resource://services-sync/record.js");
Cu.import("resource://services-sync/service.js");
Cu.import("resource://services-sync/status.js");
Cu.import("resource://services-sync/util.js");

add_test(function test_locally_changed_keys() {
  let passphrase = "abcdeabcdeabcdeabcdeabcdea";

  let hmacErrorCount = 0;
  function counting(f) {
    return function() {
      hmacErrorCount++;
      return f.call(this);
    };
  }

  Service.handleHMACEvent = counting(Service.handleHMACEvent);

  let server  = new SyncServer();
  let johndoe = server.registerUser("johndoe", "password");
  johndoe.createContents({
    meta: {},
    crypto: {},
    clients: {}
  });
  server.start();

  try {
    Svc.Prefs.set("registerEngines", "Tab");
    _("Set up some tabs.");
    let myTabs =
      {windows: [{tabs: [{index: 1,
                          entries: [{
                            url: "http://foo.com/",
                            title: "Title"
                          }],
                          attributes: {
                            image: "image"
                          },
                          extData: {
                            weaveLastUsed: 1
                          }}]}]};
    delete Svc.Session;
    Svc.Session = {
      getBrowserState: function () JSON.stringify(myTabs)
    };

    setBasicCredentials("johndoe", "password", passphrase);
    Service.serverURL = TEST_SERVER_URL;
    Service.clusterURL = TEST_CLUSTER_URL;

    Service.engineManager.register(HistoryEngine);

    function corrupt_local_keys() {
      Service.collectionKeys._default.keyPair = [Svc.Crypto.generateRandomKey(),
                                                 Svc.Crypto.generateRandomKey()];
    }

    _("Setting meta.");

    // Bump version on the server.
    let m = new WBORecord("meta", "global");
    m.payload = {"syncID": "foooooooooooooooooooooooooo",
                 "storageVersion": STORAGE_VERSION};
    m.upload(Service.resource(Service.metaURL));

    _("New meta/global: " + JSON.stringify(johndoe.collection("meta").wbo("global")));

    // Upload keys.
    generateNewKeys(Service.collectionKeys);
    let serverKeys = Service.collectionKeys.asWBO("crypto", "keys");
    serverKeys.encrypt(Service.identity.syncKeyBundle);
    do_check_true(serverKeys.upload(Service.resource(Service.cryptoKeysURL)).success);

    // Check that login works.
    do_check_true(Service.login("johndoe", "ilovejane", passphrase));
    do_check_true(Service.isLoggedIn);

    // Sync should upload records.
    Service.sync();

    // Tabs exist.
    _("Tabs modified: " + johndoe.modified("tabs"));
    do_check_true(johndoe.modified("tabs") > 0);

    let coll_modified = Service.collectionKeys.lastModified;

    // Let's create some server side history records.
    let liveKeys = Service.collectionKeys.keyForCollection("history");
    _("Keys now: " + liveKeys.keyPair);
    let visitType = Ci.nsINavHistoryService.TRANSITION_LINK;
    let history   = johndoe.createCollection("history");
    for (let i = 0; i < 5; i++) {
      let id = 'record-no--' + i;
      let modified = Date.now()/1000 - 60*(i+10);

      let w = new CryptoWrapper("history", "id");
      w.cleartext = {
        id: id,
        histUri: "http://foo/bar?" + id,
        title: id,
        sortindex: i,
        visits: [{date: (modified - 5) * 1000000, type: visitType}],
        deleted: false};
      w.encrypt(liveKeys);

      let payload = {ciphertext: w.ciphertext,
                     IV:         w.IV,
                     hmac:       w.hmac};
      history.insert(id, payload, modified);
    }

    history.timestamp = Date.now() / 1000;
    let old_key_time = johndoe.modified("crypto");
    _("Old key time: " + old_key_time);

    // Check that we can decrypt one.
    let rec = new CryptoWrapper("history", "record-no--0");
    rec.fetch(Service.resource(Service.storageURL + "history/record-no--0"));
    _(JSON.stringify(rec));
    do_check_true(!!rec.decrypt(liveKeys));

    do_check_eq(hmacErrorCount, 0);

    // Fill local key cache with bad data.
    corrupt_local_keys();
    _("Keys now: " + Service.collectionKeys.keyForCollection("history").keyPair);

    do_check_eq(hmacErrorCount, 0);

    _("HMAC error count: " + hmacErrorCount);
    // Now syncing should succeed, after one HMAC error.
    Service.sync();
    do_check_eq(hmacErrorCount, 1);
    _("Keys now: " + Service.collectionKeys.keyForCollection("history").keyPair);

    // And look! We downloaded history!
    let store = Service.engineManager.get("history")._store;
    do_check_true(store.urlExists("http://foo/bar?record-no--0"));
    do_check_true(store.urlExists("http://foo/bar?record-no--1"));
    do_check_true(store.urlExists("http://foo/bar?record-no--2"));
    do_check_true(store.urlExists("http://foo/bar?record-no--3"));
    do_check_true(store.urlExists("http://foo/bar?record-no--4"));
    do_check_eq(hmacErrorCount, 1);

    _("Busting some new server values.");
    // Now what happens if we corrupt the HMAC on the server?
    for (let i = 5; i < 10; i++) {
      let id = 'record-no--' + i;
      let modified = 1 + (Date.now() / 1000);

      let w = new CryptoWrapper("history", "id");
      w.cleartext = {
        id: id,
        histUri: "http://foo/bar?" + id,
        title: id,
        sortindex: i,
        visits: [{date: (modified - 5 ) * 1000000, type: visitType}],
        deleted: false};
      w.encrypt(Service.collectionKeys.keyForCollection("history"));
      w.hmac = w.hmac.toUpperCase();

      let payload = {ciphertext: w.ciphertext,
                     IV:         w.IV,
                     hmac:       w.hmac};
      history.insert(id, payload, modified);
    }
    history.timestamp = Date.now() / 1000;

    _("Server key time hasn't changed.");
    do_check_eq(johndoe.modified("crypto"), old_key_time);

    _("Resetting HMAC error timer.");
    Service.lastHMACEvent = 0;

    _("Syncing...");
    Service.sync();
    _("Keys now: " + Service.collectionKeys.keyForCollection("history").keyPair);
    _("Server keys have been updated, and we skipped over 5 more HMAC errors without adjusting history.");
    do_check_true(johndoe.modified("crypto") > old_key_time);
    do_check_eq(hmacErrorCount, 6);
    do_check_false(store.urlExists("http://foo/bar?record-no--5"));
    do_check_false(store.urlExists("http://foo/bar?record-no--6"));
    do_check_false(store.urlExists("http://foo/bar?record-no--7"));
    do_check_false(store.urlExists("http://foo/bar?record-no--8"));
    do_check_false(store.urlExists("http://foo/bar?record-no--9"));

  } finally {
    Svc.Prefs.resetBranch("");
    server.stop(run_next_test);
  }
});

function run_test() {
  let logger = Log4Moz.repository.rootLogger;
  Log4Moz.repository.rootLogger.addAppender(new Log4Moz.DumpAppender());

  run_next_test();
}
