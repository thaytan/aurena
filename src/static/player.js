$(document).ready(function() {
  var sendingVol = false;
  var volChange = false;

  function send_slider_volume() {
    if (sendingVol || !volChange) return;
    var curVol = $("#mastervolslider").slider( "option", "value");
    sendingVol = true; volChange = false;
    $('#mastervolval').text(Math.round(curVol * 100).toString() + '%');
    // console.log("Sending volume " + curVol.toString());
    $.ajax({ url: "../control/volume?level=" + curVol.toString(), type: 'POST' }).complete(function() { 
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
});
