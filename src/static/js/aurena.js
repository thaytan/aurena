aurena = {

websocket_listener : function()
{
  if (aurena.ws != null)
    return;

  if (!("WebSocket" in window) && ("MozWebSocket" in window))
    window.WebSocket = window.MozWebSocket;

  if ("WebSocket" in window)
  {
     var url = document.URL.replace("http", "ws") + "../client/control";
     var ws = new WebSocket(url, "aurena");
     aurena.ws = ws;

     ws.onopen = function()
     {
        $("#connstatus").html("<p>Connected</p>");
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
        $("#connstatus").html("<p>Disconnected</p>");
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
handle_event : function handle_event(data) {
  function set_vol_slider(vol, anim) {
    var s = $("#mastervolslider");

    if (!aurena.sliding && vol != s.slider("value")) {
      // $("#debug").prepend("<p>Setting vol " + vol + "</p>");
      aurena.slide_update = true;
      s.slider("option", "animate", anim);
      s.slider("value", vol);
      s.slider("option", "animate", true);
      aurena.slide_update = false;
    }
  }

  json = jQuery.parseJSON(data);
  switch (json["msg-type"]) {
    case "enrol":
      var vol = json["volume-level"];
      aurena.paused = json["paused"];
      aurena.cur_media = json["resource-id"];

      set_vol_slider (vol, false);
      aurena.update_playstate();
      break;
    case "volume":
      var vol = json["level"];
      set_vol_slider (vol, true);
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
    default:
      $("#debug").prepend("<p>Received message of type " + json["msg-type"] + ": " + data + "</p>");
      break;
  }
},

init : function() {
  var sendingVol = false;
  var volChange = false;

  aurena.sliding = false;
  aurena.slide_update = false;
  aurena.paused = true;
  aurena.cur_media = 0;

  function send_slider_volume() {
    var curVol = $("#mastervolslider").slider( "option", "value");
    $('#mastervolval').text(Math.round(curVol * 100).toString() + '%');

    if (sendingVol || !volChange || aurena.slide_update) return;
    sendingVol = true; volChange = false;
    // $("#debug").prepend("<p>Sending volume " + curVol.toString() + "</p>");
    $.ajax({
      type: 'POST',
      url: "../control/volume",
      data: { level: curVol.toString() }
    }).complete(function() {
        sendingVol = false; send_slider_volume();
      });
  }

  $("#mastervolslider").slider({
     animate: true,
     min : 0.0, max : 1.5, range : 'true', value : 1.0, step : 0.01,
     start : function(event, ui) { //$("#debug").prepend("<p>start");
                                   aurena.sliding = true; },
     stop : function(event, ui) { setTimeout(function() { aurena.sliding = false; }, 100); },
     slide : function(event, ui) { volChange = true; send_slider_volume(); },
     change : function(event, ui) { volChange = true; send_slider_volume(); }
  });
  $("#play").click(function() { $.ajax({ url: "../control/play", type: 'POST' }); });
  $("#pause").click(function() { $.ajax({ url: "../control/pause" , type: 'POST'}); });
  $("#next").click(function() { $.ajax({ url: "../control/next" , type: 'POST'}); });
  aurena.websocket_listener();
}

};

$(document).ready(function() {
  aurena.init();
});
