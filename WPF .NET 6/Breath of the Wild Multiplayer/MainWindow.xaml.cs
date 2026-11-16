using Breath_of_the_Wild_Multiplayer.MVVM.ViewModel;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;

namespace Breath_of_the_Wild_Multiplayer
{
    /// <summary>
    /// Interaction logic for MainWindow.xaml
    /// </summary>
    public partial class MainWindow : Window
    {
        public MainWindow()
        {
            CopyAppdataFiles();

            InitializeComponent();

            var viewModel = DataContext as MainViewModel;

            if(viewModel != null )
            {
                viewModel.Window = this;
                viewModel.PropertyChanged += (s, e) =>
                {
                    if (e.PropertyName == nameof(MainViewModel.disableBackground))
                        UpdateUiScale();
                };
            }

            UpdateUiScale();
        }

        // Size the launcher UI was designed for (window size minus its 1px border).
        private const double DesignWidth = 1186;
        private const double DesignHeight = 666.8;

        private void Window_SizeChanged(object sender, SizeChangedEventArgs e) => UpdateUiScale();

        /// <summary>
        /// Scales the whole UI uniformly when the launcher window is resized, so the views' fixed-size
        /// frames stay intact. The in-game overlay (disableBackground) follows Cemu's window instead and
        /// keeps its normal stretching layout.
        /// </summary>
        private void UpdateUiScale()
        {
            if (RootGrid == null)
                return;

            bool overlay = (DataContext as MainViewModel)?.disableBackground == true;
            if (overlay)
            {
                RootGrid.LayoutTransform = System.Windows.Media.Transform.Identity;
                RootGrid.Width = double.NaN;
                RootGrid.Height = double.NaN;
                RootGrid.HorizontalAlignment = HorizontalAlignment.Stretch;
                RootGrid.VerticalAlignment = VerticalAlignment.Stretch;
                return;
            }

            double availableWidth = Math.Max(1, ActualWidth - 2);
            double availableHeight = Math.Max(1, ActualHeight - 2);
            if (ActualWidth <= 0 || ActualHeight <= 0)
            {
                availableWidth = DesignWidth;
                availableHeight = DesignHeight;
            }

            double scale = Math.Min(availableWidth / DesignWidth, availableHeight / DesignHeight);
            RootGrid.Width = DesignWidth;
            RootGrid.Height = DesignHeight;
            RootGrid.HorizontalAlignment = HorizontalAlignment.Center;
            RootGrid.VerticalAlignment = VerticalAlignment.Center;
            RootGrid.LayoutTransform = new System.Windows.Media.ScaleTransform(scale, scale);
        }

        protected void CloseClick(object sender, RoutedEventArgs e)
        {
            Close();
        }

        protected void MinimizeClick(object sender, RoutedEventArgs e)
        {
            WindowState = WindowState.Minimized;
        }

        protected void titleBarDrag(object sender, MouseButtonEventArgs e)
        {
            if (Mouse.LeftButton == MouseButtonState.Pressed)
            {
                DragMove();
            }
        }

        private void CopyAppdataFiles()
        {
            string AppdataFolder = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData) + "\\BOTWM";
            List<string> Resources = Assembly.GetExecutingAssembly().GetManifestResourceNames().Where(resource => resource.Contains("AppdataFiles")).ToList();

            if (!Directory.Exists(AppdataFolder))
                Directory.CreateDirectory(AppdataFolder);

            foreach (string resource in Resources)
            {
                Stream s = Assembly.GetExecutingAssembly().GetManifestResourceStream(resource);
                string output = $"{AppdataFolder}\\{resource.Replace("Breath_of_the_Wild_Multiplayer.AppdataFiles.", "")}";
                using (FileStream AppdataFile = new FileStream(output, FileMode.Create))
                {
                    byte[] b = new byte[s.Length + 1];
                    s.Read(b, 0, Convert.ToInt32(s.Length));
                    AppdataFile.Write(b, 0, Convert.ToInt32(b.Length - 1));
                }
            }
        }
    }
}
