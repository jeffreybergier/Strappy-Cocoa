'use strict';

const fs = require('fs');
const vm = require('vm');

function fail(message) {
  throw new Error(message);
}

function expect(condition, message) {
  if (!condition) {
    fail(message);
  }
}

const pagePath = process.argv[2];
if (!pagePath) {
  fail('Usage: node webview_behavior_harness.js <generated-page.html>');
}

const pageHTML = fs.readFileSync(pagePath, 'utf8');
const scripts = Array.from(
  pageHTML.matchAll(/<script>([\s\S]*?)<\/script>/g),
  (match) => match[1]
);
const pageScript = scripts.find((script) =>
  script.includes('function clearProcessingStatus')
);
expect(pageScript, 'Generated page did not contain its runtime script.');

const body = {
  className: '',
  getAttribute() { return null; }
};
const document = {
  body,
  documentElement: { scrollHeight: 0, offsetHeight: 0, clientHeight: 0 },
  getElementById() { return null; },
  getElementsByTagName() { return []; },
  createElement() {
    return {
      className: '',
      style: {},
      appendChild() {},
      getAttribute() { return null; },
      getElementsByTagName() { return []; }
    };
  }
};
const sandbox = {
  console,
  document,
  window: {
    pageYOffset: 0,
    scrollTo() {}
  },
  clearTimeout() {},
  setTimeout() { return 1; }
};
vm.createContext(sandbox);
vm.runInContext(pageScript, sandbox, { filename: 'generated-webview.js' });

function responseAttemptRow(attempt, responseLabel, attemptLabel) {
  return {
    getAttribute(name) {
      if (name === 'data-attempt-number') return attempt;
      if (name === 'data-response-label') return responseLabel;
      if (name === 'data-attempt-label') return attemptLabel;
      return null;
    }
  };
}

expect(sandbox.responseAttemptSummary(
  responseAttemptRow('1', 'Response', 'Attempt')) === 'Response',
  'The first response displayed an unnecessary attempt number.');
expect(sandbox.responseAttemptSummary(
  responseAttemptRow('2', 'Response', 'Attempt')) ===
    'Response \u00b7 Attempt 2',
  'A retry did not display its attempt number.');
expect(sandbox.responseAttemptSummary(
  responseAttemptRow('3', 'Localized Response', 'Localized Attempt')) ===
    'Localized Response \u00b7 Localized Attempt 3',
  'A retry did not use the localized response and attempt labels.');

let collapsedGroups = [];
let interactionStates = [];
let removedStatusCount = 0;
let finalScrollPosition = null;
sandbox.collapseAPIRoundsForPrompt = (group) => collapsedGroups.push(group);
sandbox.syncProcessingInteractionState = (active, group) => {
  interactionStates.push([active, group]);
};
sandbox.decorateAPIExchangesForRows = () => {};
sandbox.decorateAPIToolGroupsForRows = () => {};
sandbox.rowsForPromptGroup = () => [];
sandbox.removeProcessingStatusNode = () => { removedStatusCount += 1; };
sandbox.setScrollTopPosition = (position) => { finalScrollPosition = position; };

function resetProcessingState(group, autoScroll, batchDepth) {
  collapsedGroups = [];
  interactionStates = [];
  removedStatusCount = 0;
  finalScrollPosition = null;
  sandbox.strappyProcessingPromptGroupKey = group;
  sandbox.strappyProcessingFinishAfterScrollGroup = group;
  sandbox.strappyProcessingFinishAfterScrollRequested = 0;
  sandbox.strappyProcessingStatus = { active: true };
  sandbox.strappyProcessingStatusDirty = 1;
  sandbox.strappyProcessingNextTick = 42;
  sandbox.strappyUpdateTimer = null;
  sandbox.strappyUpdateDue = 1;
  sandbox.strappyAutoScrollEnabled = autoScroll ? 1 : 0;
  sandbox.strappyBatchDepth = batchDepth;
  sandbox.strappyScrollAnimationTimer = null;
}

resetProcessingState('prompt-a', true, 1);
sandbox.clearProcessingStatus();
expect(sandbox.strappyProcessingFinishAfterScrollRequested === 1,
       'Final processing state was not deferred during a message batch.');
expect(sandbox.strappyProcessingPromptGroupKey === 'prompt-a',
       'Deferred processing state unlocked the prompt too early.');
expect(collapsedGroups.length === 0 && interactionStates.length === 0,
       'Deferred processing state collapsed or unlocked before scrolling.');
expect(removedStatusCount === 1 && sandbox.strappyProcessingStatus === null,
       'Deferred processing status did not remove its visible status row.');

sandbox.strappyBatchDepth = 0;
sandbox.strappyScrollAnimationGeneration = 7;
sandbox.strappyScrollAnimationDuration = 1;
sandbox.strappyScrollAnimationTimer = 1;
sandbox.scrollBottomAnimationStep(7, 0, 100, 0);
expect(finalScrollPosition === 100,
       'Completing the final scroll did not reach the target position.');
expect(sandbox.strappyProcessingPromptGroupKey === '',
       'Completing the final scroll did not clear the processing group.');
expect(collapsedGroups.includes('prompt-a'),
       'Completing the final scroll did not collapse the finished rounds.');
expect(interactionStates.some(([active, group]) =>
  active === 0 && group === 'prompt-a'),
  'Completing the final scroll did not unlock processing interactions.');

resetProcessingState('prompt-b', false, 1);
sandbox.clearProcessingStatus();
expect(sandbox.strappyProcessingFinishAfterScrollRequested === 0,
       'Disabled autoscroll left final processing completion pending.');
expect(sandbox.strappyProcessingPromptGroupKey === '',
       'Disabled autoscroll did not finish processing immediately.');
expect(collapsedGroups.includes('prompt-b'),
       'Disabled autoscroll did not collapse the finished rounds.');

let existingRoundRows = [];
sandbox.processingInteractionsLocked = () => 1;
sandbox.rowIsAPIExchangeAnswer = (row) => row.answer ? 1 : 0;
sandbox.apiRoundId = (row) => row.round || '';
sandbox.apiExchangeDirection = (row) => row.direction || '';
sandbox.apiExchangeKind = (row) => row.kind || '';
sandbox.promptGroupKey = (row) => row.group || '';
sandbox.promptGroupIsProcessing = (group) => group === 'prompt-c' ? 1 : 0;
sandbox.rowsForRound = () => existingRoundRows;

const answer = { answer: true, round: 'round-1', group: 'prompt-c' };
const functionCall = {
  round: 'round-1',
  direction: 'response',
  kind: 'function_call'
};
expect(sandbox.processingFinalAnswerGroup([answer]) === 'prompt-c',
       'A terminal assistant answer was not recognized.');
expect(sandbox.processingFinalAnswerGroup([answer, functionCall]) === '',
       'A response function call was mistaken for a terminal answer.');
existingRoundRows = [functionCall];
expect(sandbox.processingFinalAnswerGroup([answer]) === '',
       'An existing response function call was mistaken for a terminal answer.');
sandbox.processingInteractionsLocked = () => 0;
expect(sandbox.processingFinalAnswerGroup([answer]) === '',
       'An unlocked prompt was mistaken for an active terminal answer.');
