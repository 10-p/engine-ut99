// ufront 2.92 — a multisampled emulated back buffer, for the render devices that draw into framebuffer 0.
//
// Under -sOFFSCREEN_FRAMEBUFFER (2.43: the explicit present) Emscripten draws "framebuffer 0" into a framebuffer of
// its own (libwebgl.js createOffscreenFramebuffer: a single-sample RGBA texture + a depth renderbuffer) and copies it
// to the canvas at emscripten_webgl_commit_frame. The context's `antialias` attribute reaches only the canvas, never
// that framebuffer, so NOpenGLESDrv and OpenGLDrv/UTGLR (which draw into framebuffer 0) could not antialias at all;
// XOpenGLDrv draws into multisampled framebuffers of its own and is not concerned.
//
// ufront_backbuffer_samples(n), called by a render device on its own context: n > 1 on WebGL 2 swaps the emulated
// framebuffer for a multisampled one (n samples, clamped to MAX_SAMPLES; RGBA8 + DEPTH24_STENCIL8, the depth the
// 2.72 offscreen-depth24 rewrite gives the single-sample one). Emscripten's own framebuffer becomes its resolve
// target: the present blits the samples into it, then copies it to the canvas as before; a read of framebuffer 0
// (readPixels, copyTex[Sub]Image2D: screenshots, save thumbnails) resolves first and reads the resolve. n < 2 puts
// Emscripten's framebuffer back. Returns the samples in use: 0 when off, on WebGL 1 (no multisampled renderbuffers)
// or without an emulated framebuffer.
mergeInto(LibraryManager.library, {
  $ufrontMsaa__deps: ['$GL'],
  $ufrontMsaa__postset: 'ufrontMsaa.install();',
  $ufrontMsaa: {
    install() {
      var resize = GL.resizeOffscreenFramebuffer;
      var blit = GL.blitOffscreenFramebuffer;
      GL.resizeOffscreenFramebuffer = (context) => {
        resize(context);
        if (context.ufrontMsaa) ufrontMsaa.storage(context);
      };
      GL.blitOffscreenFramebuffer = (context) => {
        var m = context.ufrontMsaa;
        if (!m) return blit(context);
        ufrontMsaa.resolve(context);
        // Emscripten's copy to the canvas reads `defaultFbo`: give it the resolve for the time of the copy.
        context.defaultFbo = m.resolveFbo;
        try {
          blit(context);
        } finally {
          context.defaultFbo = m.fbo;
        }
        // blit restores the framebuffer that was bound; if that was the resolve (the swap above), draw into the
        // samples again.
        var gl = context.GLctx;
        if (gl.getParameter(0x8CA6 /*GL_DRAW_FRAMEBUFFER_BINDING*/) === m.resolveFbo) gl.bindFramebuffer(0x8D40 /*GL_FRAMEBUFFER*/, m.fbo);
      };
    },

    // (Re)allocates the multisampled storage at the drawing buffer's size; false when the driver refuses it.
    storage(context) {
      var gl = context.GLctx, m = context.ufrontMsaa;
      var w = gl.drawingBufferWidth, h = gl.drawingBufferHeight;
      var prevRb = gl.getParameter(0x8CA7 /*GL_RENDERBUFFER_BINDING*/);
      gl.bindRenderbuffer(0x8D41 /*GL_RENDERBUFFER*/, m.color);
      gl.renderbufferStorageMultisample(0x8D41, m.samples, 0x8058 /*GL_RGBA8*/, w, h);
      gl.bindRenderbuffer(0x8D41, m.depth);
      gl.renderbufferStorageMultisample(0x8D41, m.samples, 0x88F0 /*GL_DEPTH24_STENCIL8*/, w, h);
      gl.bindRenderbuffer(0x8D41, prevRb);
      var prevFb = gl.getParameter(0x8CA6 /*GL_DRAW_FRAMEBUFFER_BINDING*/);
      gl.bindFramebuffer(0x8D40 /*GL_FRAMEBUFFER*/, m.fbo);
      gl.framebufferRenderbuffer(0x8D40, 0x8CE0 /*GL_COLOR_ATTACHMENT0*/, 0x8D41, m.color);
      gl.framebufferRenderbuffer(0x8D40, 0x821A /*GL_DEPTH_STENCIL_ATTACHMENT*/, 0x8D41, m.depth);
      var ok = gl.checkFramebufferStatus(0x8D40) === 0x8CD5 /*GL_FRAMEBUFFER_COMPLETE*/;
      gl.bindFramebuffer(0x8D40, prevFb);
      m.w = w;
      m.h = h;
      return ok;
    },

    // The samples into the single-sample resolve target (scissor off: it clips a blit).
    resolve(context) {
      var gl = context.GLctx, m = context.ufrontMsaa;
      var prevRead = gl.getParameter(0x8CAA /*GL_READ_FRAMEBUFFER_BINDING*/);
      var prevDraw = gl.getParameter(0x8CA6 /*GL_DRAW_FRAMEBUFFER_BINDING*/);
      var scissor = gl.isEnabled(0xC11 /*GL_SCISSOR_TEST*/);
      if (scissor) gl.disable(0xC11);
      gl.bindFramebuffer(0x8CA8 /*GL_READ_FRAMEBUFFER*/, m.fbo);
      gl.bindFramebuffer(0x8CA9 /*GL_DRAW_FRAMEBUFFER*/, m.resolveFbo);
      gl.blitFramebuffer(0, 0, m.w, m.h, 0, 0, m.w, m.h, 0x4000 /*GL_COLOR_BUFFER_BIT*/, 0x2600 /*GL_NEAREST*/);
      gl.bindFramebuffer(0x8CA8, prevRead);
      gl.bindFramebuffer(0x8CA9, prevDraw);
      if (scissor) gl.enable(0xC11);
    },

    // A read of framebuffer 0 reads the resolve (WebGL 2 refuses to read a multisampled framebuffer).
    wrapReads(context) {
      var gl = context.GLctx;
      if (gl.ufrontMsaaReads) return;
      gl.ufrontMsaaReads = true;
      ['readPixels', 'copyTexImage2D', 'copyTexSubImage2D'].forEach((name) => {
        var orig = gl[name];
        gl[name] = function () {
          var m = context.ufrontMsaa;
          if (!m || gl.getParameter(0x8CAA /*GL_READ_FRAMEBUFFER_BINDING*/) !== m.fbo) return orig.apply(gl, arguments);
          ufrontMsaa.resolve(context);
          gl.bindFramebuffer(0x8CA8 /*GL_READ_FRAMEBUFFER*/, m.resolveFbo);
          try {
            return orig.apply(gl, arguments);
          } finally {
            gl.bindFramebuffer(0x8CA8, m.fbo);
          }
        };
      });
    },

    set(context, samples) {
      if (!context || !context.defaultFbo || context.version < 2) return 0;
      var gl = context.GLctx, m = context.ufrontMsaa;
      samples = Math.min(samples | 0, gl.getParameter(0x8D57 /*GL_MAX_SAMPLES*/) | 0);
      var bound = gl.getParameter(0x8CA6 /*GL_DRAW_FRAMEBUFFER_BINDING*/);
      if (samples < 2) {
        if (m) {
          context.defaultFbo = m.resolveFbo;
          if (bound === m.fbo) gl.bindFramebuffer(0x8D40 /*GL_FRAMEBUFFER*/, m.resolveFbo);
          gl.deleteFramebuffer(m.fbo);
          gl.deleteRenderbuffer(m.color);
          gl.deleteRenderbuffer(m.depth);
          context.ufrontMsaa = null;
        }
        return 0;
      }
      if (!m) {
        m = context.ufrontMsaa = {
          resolveFbo: context.defaultFbo,
          fbo: gl.createFramebuffer(),
          color: gl.createRenderbuffer(),
          depth: gl.createRenderbuffer(),
        };
      }
      m.samples = samples;
      if (!ufrontMsaa.storage(context)) {
        ufrontMsaa.set(context, 0);
        return 0;
      }
      ufrontMsaa.wrapReads(context);
      context.defaultFbo = m.fbo;
      if (bound === m.resolveFbo) gl.bindFramebuffer(0x8D40 /*GL_FRAMEBUFFER*/, m.fbo);
      return samples;
    },
  },

  ufront_backbuffer_samples__deps: ['$ufrontMsaa', '$GL'],
  ufront_backbuffer_samples: (samples) => ufrontMsaa.set(GL.currentContext, samples),
});
