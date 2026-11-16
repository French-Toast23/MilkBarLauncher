using System;
using System.Collections.Generic;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace Breath_of_the_Wild_Multiplayer.Source_files
{
    /// <summary>
    /// Shown when Connect is pressed: how many players play on this PC, and which Cemu folder each one uses.
    /// Player 1 plays in this launcher; every other player gets a launcher of their own (see <see cref="LaunchArgs"/>).
    /// </summary>
    public class PlayerSetupWindow : Window
    {
        private static readonly Brush Back = new SolidColorBrush(Color.FromRgb(0x1C, 0x1C, 0x1C));
        private static readonly Brush Panel = new SolidColorBrush(Color.FromRgb(0x2A, 0x2A, 0x2A));
        private static readonly Brush Accent = new SolidColorBrush(Color.FromRgb(0x46, 0x7D, 0xE3));
        private static readonly FontFamily Font = new FontFamily("Roboto");

        private readonly List<int> _slots;          // configured Cemu slots, in order
        private readonly List<ComboBox> _pickers = new List<ComboBox>();
        private readonly List<FrameworkElement> _rows = new List<FrameworkElement>();
        private readonly List<Button> _countButtons = new List<Button>();
        private readonly TextBlock _error;
        private int _count = 1;

        /// <summary>Cemu slot per player (index 0 = this launcher), set when the player confirms.</summary>
        public List<int> ChosenSlots { get; private set; }

        public PlayerSetupWindow(string serverName)
        {
            _slots = CemuProfiles.ConfiguredSlots.ToList();

            Title = "Players on this PC";
            Width = 460;
            SizeToContent = SizeToContent.Height;
            ResizeMode = ResizeMode.NoResize;
            WindowStartupLocation = WindowStartupLocation.CenterOwner;
            Background = Back;
            Topmost = true;

            var root = new StackPanel { Margin = new Thickness(20) };

            root.Children.Add(Text(string.IsNullOrEmpty(serverName) ? "Connect" : $"Connect to {serverName}", 24, FontWeights.Bold));
            root.Children.Add(Text("How many players on this PC?", 18, FontWeights.Normal, new Thickness(0, 12, 0, 6)));

            var counts = new StackPanel { Orientation = Orientation.Horizontal };
            for (int n = 1; n <= CemuProfiles.SlotCount; n++)
            {
                int value = n;
                var b = new Button
                {
                    Content = n.ToString(), Width = 52, Height = 40, Margin = new Thickness(0, 0, 8, 0),
                    FontFamily = Font, FontSize = 20, Foreground = Brushes.White, BorderThickness = new Thickness(0),
                    IsEnabled = n <= _slots.Count, Cursor = System.Windows.Input.Cursors.Hand
                };
                b.Click += (s, e) => SetCount(value);
                _countButtons.Add(b);
                counts.Children.Add(b);
            }
            root.Children.Add(counts);

            if (_slots.Count < CemuProfiles.SlotCount)
                root.Children.Add(Text($"{_slots.Count} Cemu folder(s) set. Add more in Settings to play with more players.", 13, FontWeights.Normal, new Thickness(0, 6, 0, 0), Brushes.Gray));

            root.Children.Add(Text("Cemu for each player", 18, FontWeights.Normal, new Thickness(0, 16, 0, 6)));
            for (int p = 0; p < CemuProfiles.SlotCount; p++)
            {
                var row = new DockPanel { Margin = new Thickness(0, 0, 0, 8) };
                var label = Text(p == 0 ? "Player 1 (this window)" : $"Player {p + 1}", 16, FontWeights.Normal);
                label.Width = 190;
                label.VerticalAlignment = VerticalAlignment.Center;
                DockPanel.SetDock(label, Dock.Left);
                row.Children.Add(label);

                var picker = new ComboBox { FontFamily = Font, FontSize = 15, Height = 32, VerticalContentAlignment = VerticalAlignment.Center };
                foreach (int slot in _slots)
                    picker.Items.Add(CemuProfiles.Describe(slot));
                picker.SelectedIndex = DefaultPick(p);
                row.Children.Add(picker);

                _pickers.Add(picker);
                _rows.Add(row);
                root.Children.Add(row);
            }

            _error = Text("", 14, FontWeights.Normal, new Thickness(0, 4, 0, 0), new SolidColorBrush(Color.FromRgb(0xE3, 0x5D, 0x5D)));
            _error.TextWrapping = TextWrapping.Wrap;
            root.Children.Add(_error);

            var buttons = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right, Margin = new Thickness(0, 14, 0, 0) };
            var cancel = ActionButton("Cancel", Panel);
            cancel.Click += (s, e) => { DialogResult = false; };
            var ok = ActionButton("Connect", Accent);
            ok.IsDefault = true;
            ok.Click += (s, e) => Confirm();
            buttons.Children.Add(cancel);
            buttons.Children.Add(ok);
            root.Children.Add(buttons);

            Content = root;

            if (_slots.Count == 0)
            {
                _error.Text = "No Cemu folder is set. Choose your Cemu.exe in Settings first.";
                ok.IsEnabled = false;
            }
            SetCount(1);
        }

        /// <summary>Player 1 defaults to the slot already chosen above Connect, the others to the next free slots.</summary>
        private int DefaultPick(int player)
        {
            if (_slots.Count == 0) return -1;
            int first = Math.Max(0, _slots.IndexOf(CemuProfiles.Active));
            return (first + player) % _slots.Count;
        }

        private void SetCount(int n)
        {
            _count = Math.Max(1, Math.Min(n, Math.Max(1, _slots.Count)));
            for (int i = 0; i < _rows.Count; i++)
                _rows[i].Visibility = i < _count ? Visibility.Visible : Visibility.Collapsed;
            for (int i = 0; i < _countButtons.Count; i++)
                _countButtons[i].Background = i + 1 == _count ? Accent : Panel;
            _error.Text = "";
        }

        private void Confirm()
        {
            var chosen = _pickers.Take(_count).Select(p => p.SelectedIndex).ToList();
            if (chosen.Any(i => i < 0))
            {
                _error.Text = "Pick a Cemu for every player.";
                return;
            }

            var slots = chosen.Select(i => _slots[i]).ToList();
            if (slots.Distinct().Count() != slots.Count)
            {
                _error.Text = "Each player needs a different Cemu.";
                return;
            }

            var running = slots.Where(CemuProfiles.IsSlotRunning).ToList();
            if (running.Any())
            {
                _error.Text = string.Join(", ", running.Select(s => $"Cemu {s + 1}")) + " is already running. Close it or pick another one.";
                return;
            }

            ChosenSlots = slots;
            DialogResult = true;
        }

        private static TextBlock Text(string text, double size, FontWeight weight, Thickness? margin = null, Brush color = null) => new TextBlock
        {
            Text = text, FontFamily = Font, FontSize = size, FontWeight = weight, FontStyle = FontStyles.Italic,
            Foreground = color ?? Brushes.White, Margin = margin ?? new Thickness(0)
        };

        private static Button ActionButton(string text, Brush background) => new Button
        {
            Content = text, MinWidth = 110, Height = 40, Margin = new Thickness(8, 0, 0, 0), Padding = new Thickness(14, 0, 14, 0),
            FontFamily = Font, FontSize = 18, FontStyle = FontStyles.Italic, Foreground = Brushes.White,
            Background = background, BorderThickness = new Thickness(0), Cursor = System.Windows.Input.Cursors.Hand
        };
    }
}
