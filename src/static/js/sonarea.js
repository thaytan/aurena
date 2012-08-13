function web_socket_listener()
{
  if (!("WebSocket" in window) && ("MozWebSocket" in window))
    window.WebSocket = window.MozWebSocket;

  if ("WebSocket" in window)
  {
     // Let us open a web socket
     var ws = new WebSocket("ws://localhost:5457/status", "sonarea");
     ws.onopen = function()
     {
        setInterval (function() {
          ws.send("{ string: \"short test message\"; }");
          $("#debug").prepend("<p>Sent test message</p>");
         }, 10000);
     };
     ws.onmessage = function (evt)
     {
        var received_msg = evt.data;
        $("#debug").prepend("<p>Received message: " + received_msg + "</p>");
     };
     ws.onclose = function()
     {
        $("#debug").prepend("<p>Lost websocket connection</p>");
     };
  }
  else
  {
     $("#debug").prepend("<p>websocket connections not available!</p>");
  }
}

$(document).ready(function() {
  var sendingVol = false;
  var volChange = false;

  function send_slider_volume() {
    if (sendingVol || !volChange) return;
    var curVol = $("#mastervolslider").slider( "option", "value");
    sendingVol = true; volChange = false;
    $('#mastervolval').text(Math.round(curVol * 100).toString() + '%');
    // console.log("Sending volume " + curVol.toString());
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
     slide : function(event, ui) { volChange = true; send_slider_volume(); },
     change : function(event, ui) { volChange = true; send_slider_volume(); }
  });
  $("#play").click(function() { $.ajax({ url: "../control/play", type: 'POST' }); });
  $("#pause").click(function() { $.ajax({ url: "../control/pause" , type: 'POST'}); });
  $("#next").click(function() { $.ajax({ url: "../control/next" , type: 'POST'}); });
  web_socket_listener();
});
