Module["preRun"] = Module["preRun"] || [];
Module["preRun"].push(function() {
  if (typeof Module["INIT_SONG"] === "string" && Module["INIT_SONG"].length)
    ENV["INIT_SONG"] = Module["INIT_SONG"];
  if (typeof Module["INIT_PLAY"] === "string" && Module["INIT_PLAY"].length)
    ENV["INIT_PLAY"] = Module["INIT_PLAY"];
});
