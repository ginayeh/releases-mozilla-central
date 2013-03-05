/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests the objects defined in the "DownloadCore" module.
 */

"use strict";

////////////////////////////////////////////////////////////////////////////////
//// Tests

/**
 * Executes a download, started by constructing the simplest Download object.
 */
add_task(function test_download_construction()
{
  let targetFile = getTempFile(TEST_TARGET_FILE_NAME);

  let download = yield Downloads.createDownload({
    source: { uri: TEST_SOURCE_URI },
    target: { file: targetFile },
    saver: { type: "copy" },
  });

  // Checks the generated DownloadSource and DownloadTarget properties.
  do_check_true(download.source.uri.equals(TEST_SOURCE_URI));
  do_check_eq(download.target.file, targetFile);

  // Starts the download and waits for completion.
  yield download.start();

  yield promiseVerifyContents(targetFile, TEST_DATA_SHORT);
});

/**
 * Checks initial and final state and progress for a successful download.
 */
add_task(function test_download_initial_final_state()
{
  let download = yield promiseSimpleDownload();

  do_check_true(download.stopped);
  do_check_false(download.succeeded);
  do_check_false(download.canceled);
  do_check_true(download.error === null);
  do_check_eq(download.progress, 0);

  // Starts the download and waits for completion.
  yield download.start();

  do_check_true(download.stopped);
  do_check_true(download.succeeded);
  do_check_false(download.canceled);
  do_check_true(download.error === null);
  do_check_eq(download.progress, 100);
});

/**
 * Checks the notification of the final download state.
 */
add_task(function test_download_final_state_notified()
{
  let download = yield promiseSimpleDownload();

  let onchangeNotified = false;
  let lastNotifiedStopped;
  let lastNotifiedProgress;
  download.onchange = function () {
    onchangeNotified = true;
    lastNotifiedStopped = download.stopped;
    lastNotifiedProgress = download.progress;
  };

  // Starts the download and waits for completion.
  yield download.start();

  // The view should have been notified before the download completes.
  do_check_true(onchangeNotified);
  do_check_true(lastNotifiedStopped);
  do_check_eq(lastNotifiedProgress, 100);
});

/**
 * Checks intermediate progress for a successful download.
 */
add_task(function test_download_intermediate_progress()
{
  let interruptible = startInterruptibleResponseHandler();

  let download = yield promiseSimpleDownload(TEST_INTERRUPTIBLE_URI);

  download.onchange = function () {
    if (download.progress == 50) {
      do_check_true(download.hasProgress);
      do_check_eq(download.currentBytes, TEST_DATA_SHORT.length);
      do_check_eq(download.totalBytes, TEST_DATA_SHORT.length * 2);

      // Continue after the first chunk of data is fully received.
      interruptible.resolve();
    }
  };

  // Starts the download and waits for completion.
  yield download.start();

  do_check_true(download.stopped);
  do_check_eq(download.progress, 100);

  yield promiseVerifyContents(download.target.file,
                              TEST_DATA_SHORT + TEST_DATA_SHORT);
});

/**
 * Calls the "start" method two times before the download is finished.
 */
add_task(function test_download_start_twice()
{
  let download = yield promiseSimpleDownload(TEST_INTERRUPTIBLE_URI);

  // Ensure that the download cannot complete before start is called twice.
  let interruptible = startInterruptibleResponseHandler();

  // Call the start method two times.
  let promiseAttempt1 = download.start();
  let promiseAttempt2 = download.start();

  // Allow the download to finish.
  interruptible.resolve();

  // Both promises should now be resolved.
  yield promiseAttempt1;
  yield promiseAttempt2;

  do_check_true(download.stopped);
  do_check_true(download.succeeded);
  do_check_false(download.canceled);
  do_check_true(download.error === null);

  yield promiseVerifyContents(download.target.file,
                              TEST_DATA_SHORT + TEST_DATA_SHORT);
});

/**
 * Cancels a download and verifies that its state is reported correctly.
 */
add_task(function test_download_cancel_midway()
{
  let download = yield promiseSimpleDownload(TEST_INTERRUPTIBLE_URI);

  let interruptible = startInterruptibleResponseHandler();
  try {
    // Cancel the download after receiving the first part of the response.
    let deferCancel = Promise.defer();
    download.onchange = function () {
      if (!download.stopped && !download.canceled && download.progress == 50) {
        deferCancel.resolve(download.cancel());

        // The state change happens immediately after calling "cancel", but
        // temporary files or part files may still exist at this point.
        do_check_true(download.canceled);
      }
    };

    let promiseAttempt = download.start();

    // Wait on the promise returned by the "cancel" method to ensure that the
    // cancellation process finished and temporary files were removed.
    yield deferCancel.promise;

    do_check_true(download.stopped);
    do_check_true(download.canceled);
    do_check_true(download.error === null);

    do_check_false(download.target.file.exists());

    // Progress properties are not reset by canceling.
    do_check_eq(download.progress, 50);
    do_check_eq(download.totalBytes, TEST_DATA_SHORT.length * 2);
    do_check_eq(download.currentBytes, TEST_DATA_SHORT.length);

    // The promise returned by "start" should have been rejected meanwhile.
    try {
      yield promiseAttempt;
      do_throw("The download should have been canceled.");
    } catch (ex if ex instanceof Downloads.Error) {
      do_check_false(ex.becauseSourceFailed);
      do_check_false(ex.becauseTargetFailed);
    }
  } finally {
    interruptible.reject();
  }
});

/**
 * Cancels a download right after starting it.
 */
add_task(function test_download_cancel_immediately()
{
  // Ensure that the download cannot complete before cancel is called.
  let interruptible = startInterruptibleResponseHandler();
  try {
    let download = yield promiseSimpleDownload(TEST_INTERRUPTIBLE_URI);

    let promiseAttempt = download.start();
    do_check_false(download.stopped);

    let promiseCancel = download.cancel();
    do_check_true(download.canceled);

    // At this point, we don't know whether the download has already stopped or
    // is still waiting for cancellation.  We can wait on the promise returned
    // by the "start" method to know for sure.
    try {
      yield promiseAttempt;
      do_throw("The download should have been canceled.");
    } catch (ex if ex instanceof Downloads.Error) {
      do_check_false(ex.becauseSourceFailed);
      do_check_false(ex.becauseTargetFailed);
    }

    do_check_true(download.stopped);
    do_check_true(download.canceled);
    do_check_true(download.error === null);

    do_check_false(download.target.file.exists());

    // Check that the promise returned by the "cancel" method has been resolved.
    yield promiseCancel;
  } finally {
    interruptible.reject();
  }
});

/**
 * Cancels and restarts a download sequentially.
 */
add_task(function test_download_cancel_midway_restart()
{
  let download = yield promiseSimpleDownload(TEST_INTERRUPTIBLE_URI);

  // The first time, cancel the download midway.
  let interruptible = startInterruptibleResponseHandler();
  try {
    let deferCancel = Promise.defer();
    download.onchange = function () {
      if (!download.stopped && !download.canceled && download.progress == 50) {
        deferCancel.resolve(download.cancel());
      }
    };
    download.start();
    yield deferCancel.promise;
  } finally {
    interruptible.reject();
  }

  do_check_true(download.stopped);

  // The second time, we'll provide the entire interruptible response.
  startInterruptibleResponseHandler().resolve();
  download.onchange = null;
  let promiseAttempt = download.start();

  // Download state should have already been reset.
  do_check_false(download.stopped);
  do_check_false(download.canceled);
  do_check_true(download.error === null);

  // For the following test, we rely on the network layer reporting its progress
  // asynchronously.  Otherwise, there is nothing stopping the restarted
  // download from reaching the same progress as the first request already.
  do_check_eq(download.progress, 0);
  do_check_eq(download.totalBytes, 0);
  do_check_eq(download.currentBytes, 0);

  // Allow the second request to complete.
  interruptible.resolve();

  yield promiseAttempt;

  do_check_true(download.stopped);
  do_check_true(download.succeeded);
  do_check_false(download.canceled);
  do_check_true(download.error === null);

  yield promiseVerifyContents(download.target.file,
                              TEST_DATA_SHORT + TEST_DATA_SHORT);
});

/**
 * Cancels a download right after starting it, then restarts it immediately.
 */
add_task(function test_download_cancel_immediately_restart_immediately()
{
  let download = yield promiseSimpleDownload(TEST_INTERRUPTIBLE_URI);

  // Ensure that the download cannot complete before cancel is called.
  let interruptible = startInterruptibleResponseHandler();

  let promiseAttempt = download.start();
  do_check_false(download.stopped);

  download.cancel();
  do_check_true(download.canceled);

  let promiseRestarted = download.start();
  do_check_false(download.stopped);
  do_check_false(download.canceled);
  do_check_true(download.error === null);

  // For the following test, we rely on the network layer reporting its progress
  // asynchronously.  Otherwise, there is nothing stopping the restarted
  // download from reaching the same progress as the first request already.
  do_check_eq(download.hasProgress, false);
  do_check_eq(download.progress, 0);
  do_check_eq(download.totalBytes, 0);
  do_check_eq(download.currentBytes, 0);

  // Allow the second request to complete.
  startInterruptibleResponseHandler().resolve();

  try {
    yield promiseAttempt;
    do_throw("The download should have been canceled.");
  } catch (ex if ex instanceof Downloads.Error) {
    do_check_false(ex.becauseSourceFailed);
    do_check_false(ex.becauseTargetFailed);
  }

  yield promiseRestarted;

  do_check_true(download.stopped);
  do_check_true(download.succeeded);
  do_check_false(download.canceled);
  do_check_true(download.error === null);

  do_check_true(download.target.file.exists());
});

/**
 * Cancels a download midway, then restarts it immediately.
 */
add_task(function test_download_cancel_midway_restart_immediately()
{
  let download = yield promiseSimpleDownload(TEST_INTERRUPTIBLE_URI);

  // The first time, cancel the download midway.
  let interruptible = startInterruptibleResponseHandler();

  let deferMidway = Promise.defer();
  download.onchange = function () {
    if (!download.stopped && !download.canceled && download.progress == 50) {
      do_check_eq(download.progress, 50);
      deferMidway.resolve();
    }
  };
  let promiseAttempt = download.start();
  yield deferMidway.promise;

  download.cancel();
  do_check_true(download.canceled);

  let promiseRestarted = download.start();
  do_check_false(download.stopped);
  do_check_false(download.canceled);
  do_check_true(download.error === null);

  // For the following test, we rely on the network layer reporting its progress
  // asynchronously.  Otherwise, there is nothing stopping the restarted
  // download from reaching the same progress as the first request already.
  do_check_eq(download.hasProgress, false);
  do_check_eq(download.progress, 0);
  do_check_eq(download.totalBytes, 0);
  do_check_eq(download.currentBytes, 0);

  interruptible.reject();

  // Allow the second request to complete.
  startInterruptibleResponseHandler().resolve();

  try {
    yield promiseAttempt;
    do_throw("The download should have been canceled.");
  } catch (ex if ex instanceof Downloads.Error) {
    do_check_false(ex.becauseSourceFailed);
    do_check_false(ex.becauseTargetFailed);
  }

  yield promiseRestarted;

  do_check_true(download.stopped);
  do_check_true(download.succeeded);
  do_check_false(download.canceled);
  do_check_true(download.error === null);

  do_check_true(download.target.file.exists());

  yield promiseVerifyContents(download.target.file,
                              TEST_DATA_SHORT + TEST_DATA_SHORT);
});

/**
 * Calls the "cancel" method on a successful download.
 */
add_task(function test_download_cancel_successful()
{
  let download = yield promiseSimpleDownload();

  // Starts the download and waits for completion.
  yield download.start();

  // The cancel method should succeed with no effect.
  yield download.cancel();

  do_check_true(download.stopped);
  do_check_true(download.succeeded);
  do_check_false(download.canceled);
  do_check_true(download.error === null);

  yield promiseVerifyContents(download.target.file, TEST_DATA_SHORT);
});

/**
 * Calls the "cancel" method two times in a row.
 */
add_task(function test_download_cancel_twice()
{
  let download = yield promiseSimpleDownload(TEST_INTERRUPTIBLE_URI);

  // Ensure that the download cannot complete before cancel is called.
  let interruptible = startInterruptibleResponseHandler();

  try {
    let promiseAttempt = download.start();
    do_check_false(download.stopped);

    let promiseCancel1 = download.cancel();
    do_check_true(download.canceled);
    let promiseCancel2 = download.cancel();

    try {
      yield promiseAttempt;
      do_throw("The download should have been canceled.");
    } catch (ex if ex instanceof Downloads.Error) {
      do_check_false(ex.becauseSourceFailed);
      do_check_false(ex.becauseTargetFailed);
    }

    // Both promises should now be resolved.
    yield promiseCancel1;
    yield promiseCancel2;

    do_check_true(download.stopped);
    do_check_false(download.succeeded);
    do_check_true(download.canceled);
    do_check_true(download.error === null);

    do_check_false(download.target.file.exists());
  } finally {
    interruptible.reject();
  }
});

/**
 * Checks that whenSucceeded returns a promise that is resolved after a restart.
 */
add_task(function test_download_whenSucceeded()
{
  let download = yield promiseSimpleDownload(TEST_INTERRUPTIBLE_URI);

  // Ensure that the download cannot complete before cancel is called.
  let interruptible = startInterruptibleResponseHandler();

  // Get a reference before the first download attempt.
  let promiseSucceeded = download.whenSucceeded();

  // Cancel the first download attempt.
  download.start();
  yield download.cancel();

  interruptible.reject();

  // Allow the second request to complete.
  startInterruptibleResponseHandler().resolve();
  download.start();

  // Wait for the download to finish by waiting on the whenSucceeded promise.
  yield promiseSucceeded;

  do_check_true(download.stopped);
  do_check_true(download.succeeded);
  do_check_false(download.canceled);
  do_check_true(download.error === null);

  do_check_true(download.target.file.exists());
});

/**
 * Ensures download error details are reported on network failures.
 */
add_task(function test_download_error_source()
{
  let serverSocket = startFakeServer();
  try {
    let download = yield promiseSimpleDownload(TEST_FAKE_SOURCE_URI);

    do_check_true(download.error === null);

    try {
      yield download.start();
      do_throw("The download should have failed.");
    } catch (ex if ex instanceof Downloads.Error && ex.becauseSourceFailed) {
      // A specific error object is thrown when reading from the source fails.
    }

    do_check_true(download.stopped);
    do_check_false(download.canceled);
    do_check_true(download.error !== null);
    do_check_true(download.error.becauseSourceFailed);
    do_check_false(download.error.becauseTargetFailed);
  } finally {
    serverSocket.close();
  }
});

/**
 * Ensures download error details are reported on local writing failures.
 */
add_task(function test_download_error_target()
{
  let download = yield promiseSimpleDownload();

  do_check_true(download.error === null);

  // Create a file without write access permissions before downloading.
  download.target.file.create(Ci.nsIFile.NORMAL_FILE_TYPE, 0);

  try {
    yield download.start();
    do_throw("The download should have failed.");
  } catch (ex if ex instanceof Downloads.Error && ex.becauseTargetFailed) {
    // A specific error object is thrown when writing to the target fails.
  }

  do_check_true(download.stopped);
  do_check_false(download.canceled);
  do_check_true(download.error !== null);
  do_check_true(download.error.becauseTargetFailed);
  do_check_false(download.error.becauseSourceFailed);
});

/**
 * Restarts a failed download.
 */
add_task(function test_download_error_restart()
{
  let download = yield promiseSimpleDownload();

  do_check_true(download.error === null);

  // Create a file without write access permissions before downloading.
  download.target.file.create(Ci.nsIFile.NORMAL_FILE_TYPE, 0);

  try {
    yield download.start();
    do_throw("The download should have failed.");
  } catch (ex if ex instanceof Downloads.Error && ex.becauseTargetFailed) {
    // A specific error object is thrown when writing to the target fails.
  }

  if (download.target.file.exists()) {
    download.target.file.remove(false);
  }

  // Restart the download and wait for completion.
  yield download.start();

  do_check_true(download.stopped);
  do_check_true(download.succeeded);
  do_check_false(download.canceled);
  do_check_true(download.error === null);
  do_check_eq(download.progress, 100);

  yield promiseVerifyContents(download.target.file, TEST_DATA_SHORT);
});
