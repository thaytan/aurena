aurena = {

websocket_listener : function()
{
  if (aurena.ws != null)
    return;

  if (!("WebSocket" in window) && ("MozWebSocket" in window))
    window.WebSocket = window.MozWebSocket;

  if ("WebSocket" in window)
  {
     var url = document.URL.replace("http", "ws") + "../client/events?roles=controller";
     var ws = new WebSocket(url, "aurena");
     aurena.ws = ws;

     ws.onopen = function()
     {
        $("#connstatus").hide();
     };
     ws.onmessage = function (evt)
     {
        var received_msg = evt.data;

        if (typeof received_msg == typeof("")) {
            aurena.handle_event(received_msg);
        }
     };
     ws.onclose = function()
     {
        $("#connstatus").html("Disconnected").show();
        aurena.ws = null;
        setTimeout(aurena.websocket_listener, 1000);
     };
  }
  else
  {
     $("#debug").prepend("<p>websocket connections not available!</p>");
  }
},
update_playstate : function() {
  if (aurena.paused) {
    $("#playstate").html("Paused");
  }
  else {
    $("#playstate").html("Playing");
  }
  if (aurena.cur_media != 0)
    $("#nowplaying").html("track " + aurena.cur_media);
  else
    $("#nowplaying").html("None");
},

set_vol_slider : function set_vol_slider(client_id, vol, anim) {
  var s;

  if (client_id == 0)
    s = $("#mastervolslider");
  else
    s = $("#volume-" + client_id);

  if (!aurena.sliding && vol != s.slider("value")) {
    // $("#debug").prepend("<p>Setting vol " + vol + "</p>");
    aurena.slide_update = true;
    s.slider("option", "animate", anim);
    s.slider("value", vol);
    s.slider("option", "animate", true);
    aurena.slide_update = false;
  }
},

add_client_message : function f(client_id, msg) {
   c = $("#clientmsgs");
   c.text(c.text() + msg);
},

set_client_enable : function f(client_id, enable, record_enable) {
  if (client_id < 1)
    return;
  var s = $("#enable-" + client_id);
  var options;

  if (enable)
    options = { text: false, icons: { primary: "aurena icon-volume-on" } };
  else
    options = { text: false, icons: { primary: "aurena icon-volume-off" } };
  s.button( "option", options ).attr('checked', enable);

  var s = $("#record-enable-" + client_id);
  if (record_enable)
    options = { text: false, icons: { primary: "aurena icon-microphone-on" } };
  else
    options = { text: false, icons: { primary: "aurena icon-microphone-off" } };
  s.button( "option", options ).attr('checked', record_enable);
},

handle_event : function handle_event(data) {
  json = $.parseJSON(data);
  switch (json["msg-type"]) {
    case "enrol":
      var vol = json["volume-level"];
      aurena.paused = json["paused"];
      aurena.cur_media = json["resource-id"];

      aurena.set_vol_slider (0, vol, false);
      aurena.update_playstate();
      break;
    case "volume":
      var vol = json["level"];
      aurena.set_vol_slider (0, vol, true);
      break;
    case "client-volume":
      var vol = json["level"];
      aurena.set_vol_slider (json["client-id"], vol, true);
      break;
    case "client-setting":
      var en = json["enabled"];
      var record_en = json["record-enabled"];
      aurena.set_client_enable (json["client-id"], en, record_en);
      break;
    case "client-message":
      aurena.add_client_message(json["client-id"], json["message"]);
      break;
    case "pause":
      aurena.paused = true;
      aurena.update_playstate();
      break;
    case "play":
      aurena.paused = false;
      aurena.update_playstate();
      break;
    case "set-media":
      aurena.paused = json["paused"];
      aurena.cur_media = json["resource-id"];
      aurena.update_playstate();
      break;
    case "player-clients-changed":
      aurena.update_player_clients();
      break;
    case "ping":
      break;
    case "client-stats":
      aurena.update_player_stats(json);
      break;
    default:
      // $("#debug").prepend("<p>Received message of type " + json["msg-type"] + ": " + data + "</p>");
      break;
  }
},
update_player_stats : function (stats) {
  client_id = stats["client-id"];
  synched = (stats["synchronised"] == true);
  max_err = Math.max (Math.abs (stats['remote-min-error']),
                      Math.abs (stats['remote-max-error']));
  rtt_avg = stats["rtt-average"];
  cur_pos = stats["position"];
  expected_pos = stats["expected-position"];

  info_str = "(RTT ";
  if (rtt_avg > 1000000)
    info_str += (rtt_avg / 1000000).toFixed(1) + "ms";
  else
    info_str += (rtt_avg / 1000).toFixed(1) + "us";

  info_str += " Max Error ";
  if (max_err > 1000000)
    info_str += (max_err / 1000000).toFixed(1) + "ms";
  else
    info_str += (max_err / 1000).toFixed(1) + "us";
  info_str += ") Position offset ";

  pos_diff = cur_pos - expected_pos;
  if (Math.abs(pos_diff) > 1000000)
    info_str += (pos_diff / 1000000).toFixed(1) + "ms";
  else
    info_str += (pos_diff / 1000).toFixed(1) + "us";

  $("#cliententries #stats-" + client_id).empty().prepend (info_str)
      .toggleClass("client-clock-synched", synched)
      .toggleClass("client-clock-not-synched", !synched);
},
update_player_clients : function () {
  function send_enable_val(client_id) {
    if (aurena.sendingEnable)
      return;
    var enabled = 0;
    var rec_enabled = 0;

    if ($("input#enable-" + client_id).is(':checked'))
      enabled = 1;
    if ($("input#record-enable-" + client_id).is(':checked'))
      rec_enabled = 1;

    aurena.sendingEnable = true;
    $.ajax({
      type: 'GET',
      url: "../control/setclient",
      data: { client_id: client_id, enable: enabled, record_enable: rec_enabled }
    }).complete(function() {
      aurena.sendingEnable = false;
    });
  }

  $.getJSON("../client/player_info", function(data) {
     var items = [];
     var clients = data['player-clients'];
     aurena.clients = clients;
     $.each(clients, function(key, val) {
       var enable_id = "enable-" + val["client-id"];
       var record_enable_id = "record-enable-" + val["client-id"];
       var volume_id = "volume-" + val["client-id"];
       var info = '<li id="' + val["client-id"] + '">';
       info += "<input type='checkbox' class='client-enable' id='" + enable_id +
               "'/><label for='" + enable_id + "'></label>";
       info += "<input type='checkbox' class='client-record' id='" + record_enable_id +
               "'/><label for='" + record_enable_id + "'></label>";
       info += " <span>Client " + val["host"] + "</span>";
       info += " <span id='stats-" + val["client-id"] + "' />";
       info += " <div id='" + volume_id + "' />";
       info += " <div id='volumeval-" + val["client-id"] + "' />";
       info += '</li>';
       items.push(info)
       // console.log ("Client data " + JSON.stringify(val));
     })
     $("#cliententries").empty().prepend($('<ul/>', {
       html: items.join('')
     }));
     $("#cliententries .client-enable").button({
       icons: { primary: "aurena icon-volume-on" }, text: false
     });
     $("#cliententries .client-record").button({
         icons: { primary: "aurena icon-microphone-on" }, text: false
     });
     $.each(clients, function(key, val) {
       var client_id = val["client-id"];
       var enable_id = "enable-" + client_id;
       var rec_enable_id = "record-enable-" + client_id;
       var volume_id = "volume-" + client_id;

       $("#" + volume_id).slider({
         animate: true,
         min : 0.0, max : 1.5, range : 'true', value : val["volume"], step : 0.01,
         start : function(event, ui) { aurena.sliding = true; },
         stop : function(event, ui) { setTimeout(function() { aurena.sliding = false; }, 100); },
         slide : function(cid) { return function(event, ui) { aurena.volChange = true; aurena.send_slider_volume(cid); } }(client_id),
         change : function(cid) { return function(event, ui) { aurena.volChange = true; aurena.send_slider_volume(cid); } }(client_id)
       });
       $('#volumeval-' + client_id).text(Math.round(val["volume"] * 100).toString() + '%');
       aurena.set_client_enable (client_id, val["enabled"], val["record-enabled"]);
       $("#" + enable_id).change(function(cid) { return function () { send_enable_val (cid) } }(client_id));
       $("#" + rec_enable_id).change(function(cid) { return function () { send_enable_val (cid) } }(client_id));
     });
  });
},

send_slider_volume : function send_slider_volume(client_id) {
  var s;

  if (client_id == 0)
    s= $("#mastervolslider");
  else
    s = $("#volume-" + client_id);

  var curVol = s.slider( "option", "value");
  if (client_id == 0)
    $('#mastervolval').text(Math.round(curVol * 100).toString() + '%');
  else
    $('#volumeval-' + client_id).text(Math.round(curVol * 100).toString() + '%');

  if (aurena.sendingVol || !aurena.volChange || aurena.slide_update) return;
  aurena.sendingVol = true; aurena.volChange = false;
  // $("#debug").prepend("<p>Sending volume " + curVol.toString() + "</p>");
  $.ajax({
    type: 'POST',
    url: "../control/volume",
    data: { level: curVol.toString(), client_id: client_id }
  }).complete(function() {
        aurena.sendingVol = false; aurena.send_slider_volume(client_id);
  });
},
playPause : function() {
  if (aurena.paused)
    aurena.play();
  else
    aurena.pause();
  aurena.paused = !aurena.paused;
},
play : function() {
  $.ajax({ url: "../control/play" , type: 'POST'});
},
pause : function() {
  $.ajax({ url: "../control/pause" , type: 'POST'});
},
next : function() {
  $.ajax({ url: "../control/next" , type: 'POST'});
},
previous : function() {
  $.ajax({ url: "../control/previous" , type: 'POST'});
},
send_control : function(cmd) {
  $.ajax({ url: "../control/" + cmd , type: 'POST'});
},
jumpToTrack : function () {
  v = $("#jumptotrackid").val();

  track=parseInt(v);
  if (!isNaN(track) || v.substring(0, 4) == "http") {
    $.ajax({ url: "../control/next", type: 'POST',
       data: { id: v } });
  }
  $("#jumptotrackid").val("");
},
init : function() {
  aurena.sendingVol = false;
  aurena.volChange = false;

  aurena.sliding = false;
  aurena.slide_update = false;
  aurena.paused = true;
  aurena.cur_media = 0;

  $("#mastervolslider").slider({
     animate: true,
     min : 0.0, max : 1.5, range : 'true', value : 1.0, step : 0.01,
     start : function(event, ui) { //$("#debug").prepend("<p>start");
                                   aurena.sliding = true; },
     stop : function(event, ui) { setTimeout(function() { aurena.sliding = false; }, 100); },
     slide : function(event, ui) { aurena.volChange = true; aurena.send_slider_volume(0); },
     change : function(event, ui) { aurena.volChange = true; aurena.send_slider_volume(0); }
  });
  $("#play").button().click(function() { $(".ui-button-icon-primary", this)
          .toggleClass("ui-icon-pause ui-icon-play"); aurena.play() });
  $("#pause").button().click(function() { aurena.pause() });
  $("#next").button().click(function() { aurena.next() });
  $("#jumptrackform").submit(function(e) { aurena.jumpToTrack(); e.preventDefault(); return false; });
  $("#frob").button().click(function() { aurena.send_control("calibration") });
  aurena.websocket_listener();
}

};

$(document).ready(function() {
  aurena.init();
});
