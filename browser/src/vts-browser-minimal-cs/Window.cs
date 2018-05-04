﻿/**
 * Copyright (c) 2017 Melown Technologies SE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * *  Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

using System;
using System.Windows.Forms;
using vts;

namespace vtsBrowserMinimalCs
{
    public partial class Window : Form
    {
        [STAThread]
        static void Main()
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new Window());
        }

        public Window()
        {
            InitializeComponent();
        }

        public void Init(object sender, EventArgs args)
        {
            Renderer.LoadGlFunctions(VtsGL.AnyGlFnc);
            map = new Map("");
            renderer = new Renderer();
            map.DataInitialize();
            map.RenderInitialize();
            map.SetMapConfigPath("https://cdn.melown.com/mario/store/melown2015/map-config/melown/Melown-Earth-Intergeo-2017/mapConfig.json");
            renderer.Initialize();
            renderer.BindLoadFunctions(map);
        }

        public void Fin(object sender, EventArgs args)
        {
            renderer.Deinitialize();
            map.RenderDeinitialize();
            map.DataDeinitialize();
            renderer = null;
            map = null;
        }

        public void Draw(object sender, EventArgs args)
        {
            var c = sender as VtsGLControl;
            if (c.Width <= 0 || c.Height <= 0)
                return;

            map.SetWindowSize((uint)c.Width, (uint)c.Height);
            map.DataTick();
            map.RenderTickPrepare(0);
            map.RenderTickRender();

            RenderOptions ro = renderer.Options;
            ro.width = (uint)c.Width;
            ro.height = (uint)c.Height;
            renderer.Options = ro;
            renderer.Render(map);
        }

        public void MapMousePress(object sender, MouseEventArgs e)
        {
            LastMouseX = e.X;
            LastMouseY = e.Y;
        }

        public void MapMouseMove(object sender, MouseEventArgs e)
        {
            int xrel = e.X - LastMouseX;
            int yrel = e.Y - LastMouseY;
            LastMouseX = e.X;
            LastMouseY = e.Y;
            if (Math.Abs(xrel) > 100 || Math.Abs(yrel) > 100)
                return; // ignore the move event if it was too far away
            double[] p = new double[3];
            p[0] = xrel;
            p[1] = yrel;
            if (e.Button == MouseButtons.Left)
                map.Pan(p);
            if (e.Button == MouseButtons.Right)
                map.Rotate(p);
        }

        public void MapMouseWheel(object sender, MouseEventArgs e)
        {
            map.Zoom(e.Delta / 120);
        }

        public int LastMouseX, LastMouseY;
        public Map map;
        public Renderer renderer;
    }
}