/*
George Lyu
georgemylyu@gmail.com
gml8.iotlamps@gmail.com
09/13/2023

Companion Halo Webapp

With Google Sheet https://docs.google.com/spreadsheets/d/1u7ttpf7Z2ptiPnrnRCzgZZ_VvdSA0b_RN__6Law3xwg
With ESP32 MCU file Companion_Halo_XYZ.ino

Handles the HTTP GET requests from ESP32 lamps to synchronize their color across WiFi.

Flicker codes are expected to have the following structure:
  <colorstate>.<millis>.<mac>
    <colorstate> is the integer index of the lamp color
    <millis> is a 4-character integer, typically the last 4 digits of the sender device's millis() call
    <mac> is the MAC string of the sender device, with omitted colons ":" and capitalized letters 
*/


// Spreadsheet constants
const spreadsheet = SpreadsheetApp.openById("1u7ttpf7Z2ptiPnrnRCzgZZ_VvdSA0b_RN__6Law3xwg"); // This spreadsheet
const dashboardSheet = spreadsheet.getSheetByName("Dashboard"); // The Dashboard sheet
const recordSheet = spreadsheet.getSheetByName("Record"); // The Record sheet
const messageSheet = spreadsheet.getSheetByName("Message"); // The Message sheet

function doGet(event = {}) {
  /*
  Handles an HTTP GET request to this webapp.
  If the GET request does NOT have a Flicker code "fc" parameter, then ths function does nothing.
  If the "fc" parameter fails to adhere to expected Flicker code structure, this function does nothing.
  If the given Flicker code is different from the sender's current Flicker code, this function treats the request has a color-change update sent from an ESP32 lamp. Updates appropriate fields and triggers webhooks.
  RETURNS
    The sender's most recent Flicker code (if the request is an update sent from a lamp, RETURNS the given Flicker code)
  */

  // Check for MAC query
  if ("id" in event.parameter) {
    const mac = event.parameter["id"];

    // Check validity of MAC parameter
    const senderRow = checkMac(mac);
    if (senderRow == -1) {return}

    // Return to sender their flicker code
    return jsonify({"fc": getFlickerCode(senderRow)});
  }

  // Check for Flicker code query
  if ("fc" in event.parameter) {
    const flickerCode = event.parameter["fc"];

    // Check validity of flicker code
    if (flickerCode.includes("=")) {return}

    // Update Record sheet and Message sheet
    recordFlicker(flickerCode);

    // Get parameters
    const parm = flickerCode.split(".");
    if (parm.length != 3) {return}
    const colorstate = parm[0];
    const millis = parm[1];
    const mac = parm[2];

    // Check validitiy of parameters
    if (checkColorstate(colorstate) == -1) {return}
    if (checkMillis(millis) == -1) {return}
    const senderRow = checkMac(mac); // The relative row index of the sender user in the Dashboard sheet
    if (senderRow == -1) {return}

    // If sent flicker code is a new flicker code, log receipt of flicker code
    if (flickerCode != getFlickerCode(senderRow)) {

      // Update the Dashboard sheet
      affectedGroups = updateFlickerCodes(senderRow, flickerCode);

      // Send Discord webhooks
      for (groupCol of affectedGroups) {
        sendWebhook(groupCol);
      }
    }

    // Return to sender their flicker code
    return jsonify({"fc": getFlickerCode(senderRow)});
  }
}

function recordFlicker(flickerCode) {
  /*
  Logs the given Flicker code in the Record and Message sheets
  INPUT
    flickerCode, a valid Flicker code
  */

  // Log flicker code to Message sheet
  messageSheet.getRange(2, 1).setValue(flickerCode);

  // Get next row for Record sheet
  const nextRow = recordSheet.getLastRow() + 1;

  // Record timestamp
  recordSheet.getRange(nextRow, 1).setValue((new Date()).toString());

  // Record Flicker code
  recordSheet.getRange(nextRow, 2).setValue(flickerCode);
}

function updateFlickerCodes(senderRow, flickerCode) {
  /*
  Logs the given Flicker code in the Dashboard sheet
  INPUT
    senderRow; the relative row index of the sender user in the Dashboard sheet
    flickerCode; a valid Flicker code
  RETURNS
    array of the relative column indices of the groups the sender is a member of
  */

  // Number of groups in the Dashboard sheet
  let nGroups = dashboardSheet.getLastColumn() - 4;

  // Increment the sender's flicker count
  dashboardSheet.getRange(3 + senderRow, 4).setValue(dashboardSheet.getRange(3 + senderRow, 4).getValue() + 1);
  
  // Stores the relative column indices of the groups that the sender is a member of
  let affectedGroups = [];

  // For each group the sender is a member of, check for all users in that group. Update each of those users' latest Flicker codes 
  for (let groupCol = 1; groupCol <= nGroups; groupCol ++) {
    if (isUserGroupMember(senderRow, groupCol)) {

      // Increment the group's flicker count
      dashboardSheet.getRange(3, 4 + groupCol).setValue(dashboardSheet.getRange(3, 4 + groupCol).getValue() + 1);
      affectedGroups.push(groupCol);
      for (let userRow = 1; userRow <= getNumUsers(); userRow ++) {
        if (isUserGroupMember(userRow, groupCol)) {

          // Update the Flicker code
          dashboardSheet.getRange(3 + userRow, 3).setValue(flickerCode);
        }
      }
    }
  }
  return affectedGroups
}

function sendWebhook(groupCol) {
  /*
  Sends webhook message to the group with the given relative column index in Dashboard sheet
  INPUT
    relative colum index of affected group in Dashboard sheet
  */
  const url = dashboardSheet.getRange(2, 4 + groupCol).getValue(); // webhook URL
  const msg = messageSheet.getRange(2, 2).getValue();  // webhook message
  const options = {
    "method": "post",
    "payload": {"content": msg}
    };
  UrlFetchApp.fetch(url, options);
}

function isUserGroupMember(userRow, groupCol) {
  /*
  RETURNS if the user at the given relative row index is a member of the group at the given relative column index in Dashboard sheet
  */
  return Boolean(dashboardSheet.getRange(3 + userRow, 4 + groupCol).getValue());
}

function getNumUsers() {
  /*
  RETURNS the number of users found in the Dashboard sheet
  */
  return dashboardSheet.getLastRow() - 3; 
}

function getFlickerCode(userRow) {
  /*
  RETURNS the flicker code of the user at the given relative row index in Dashboard sheet
  */
  return dashboardSheet.getRange(3 + userRow, 3).getValue();
}

function jsonify(json) {
  /*
  RETURNS JSON format of the given input string
  */
  return ContentService.createTextOutput(JSON.stringify(json)).setMimeType(ContentService.MimeType.JSON);
}

function checkColorstate(colorstate) {
  /*
  RETURNS the index of the given colorstate within the Message sheet. RETURNS -1 if none are found.
  RETURNS -1 if the input colorstate string constains "=" to prevent equations from being given to the spreadsheet for security.
  */
  if (colorstate.includes("=")) {
    return -1;
  }
  nColors = messageSheet.getLastRow() - 5;
  for (let ind = 1; ind <= nColors; ind ++) {
    if (messageSheet.getRange(5 + ind, 1).getValue() == colorstate) {
      return ind;
    }
  }
  return -1;
}

function checkMac(mac) {
  /*
  RETURNS the index of the given mac within the Dashboard sheet. RETURNS -1 if none are found.
  RETURNS -1 if the input mac string constains "=" to prevent equations from being given to the spreadsheet for security.
  */
  if (mac.includes("=")) {
    return -1;
  }
  for (let userRow = 1; userRow <= getNumUsers(); userRow++) {
    if (dashboardSheet.getRange(3 + userRow, 2).getValue() == mac) {
      return userRow;
    }
  }
  return -1;
}

function checkMillis(millis) {
  /*
  RETURNS 1 if millis is a 4-digit integer. Else, -1.
  */
  if (millis.includes("=")) {
    return -1;
  }
  if (isNaN(parseInt(millis))) {
    return -1;
  }
  if (parseInt(millis) > 9999) {
    return -1;
  }
  if (parseInt(millis) < 0) {
    return -1;
  }
  return 1;
}