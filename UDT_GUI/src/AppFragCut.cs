using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;
using System.Threading;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Media;


namespace Uber.DemoTools
{
    public class CutByFragComponent : AppComponent
    {
        public FrameworkElement RootControl { get; private set; }
        public List<ListView> ListViews { get { return null; } }
        public ComponentType Type { get { return ComponentType.CutByChat; } }

        public CutByFragComponent(App app)
        {
            _app = app;
            RootControl = CreateCutByFragTab();
        }

        public void PopulateViews(DemoInfo demoInfo)
        { 
            // Nothing to do.
        }

        public void SaveToConfigObject(UdtConfig config)
        {
            int intValue = 0;

            if(App.GetOffsetSeconds(_startTimeOffsetEditBox.Text, out intValue))
            {
                _app.Config.FragCutStartOffset = intValue;
            }

            if(App.GetOffsetSeconds(_endTimeOffsetEditBox.Text, out intValue))
            {
                _app.Config.FragCutEndOffset = intValue;
            }

            if(int.TryParse(_minFragCountEditBox.Text, out intValue) && intValue > 2)
            {
                _app.Config.FragCutMinFragCount = intValue;
            }

            if(int.TryParse(_timeBetweenFragsEditBox.Text, out intValue) && intValue > 0)
            {
                _app.Config.FragCutTimeBetweenFrags = intValue;
            }

            _app.Config.FragCutAllowSelfKills = _allowSelfKillsCheckBox.IsChecked.HasValue && _allowSelfKillsCheckBox.IsChecked.Value;
            _app.Config.FragCutAllowTeamKills = _allowTeamKillsCheckBox.IsChecked.HasValue && _allowTeamKillsCheckBox.IsChecked.Value;
            _app.Config.FragCutAllowAnyDeath = _allowAnyDeathCheckBox.IsChecked.HasValue && _allowAnyDeathCheckBox.IsChecked.Value;
        }

        private App _app;
        private TextBox _startTimeOffsetEditBox = null;
        private TextBox _endTimeOffsetEditBox = null;
        private TextBox _minFragCountEditBox = null;
        private TextBox _timeBetweenFragsEditBox = null;
        private CheckBox _allowSelfKillsCheckBox = null;
        private CheckBox _allowTeamKillsCheckBox = null;
        private CheckBox _allowAnyDeathCheckBox = null;
        private RadioButton _autoPlayerSelectionRadioButton = null;
        private RadioButton _manualPlayerSelectionRadioButton = null;
        private TextBox _playerIndexTextBox = null;

        private FrameworkElement CreateCutByFragTab()
        {
            var minFragCountEditBox = new TextBox();
            _minFragCountEditBox = minFragCountEditBox;
            minFragCountEditBox.Width = 40;
            minFragCountEditBox.Text = _app.Config.FragCutMinFragCount.ToString();

            var timeBetweenFragsEditBox = new TextBox();
            _timeBetweenFragsEditBox = timeBetweenFragsEditBox;
            timeBetweenFragsEditBox.Width = 40;
            timeBetweenFragsEditBox.Text = _app.Config.FragCutTimeBetweenFrags.ToString();

            var allowSelfKillsCheckBox = new CheckBox();
            _allowSelfKillsCheckBox = allowSelfKillsCheckBox;
            allowSelfKillsCheckBox.HorizontalAlignment = HorizontalAlignment.Left;
            allowSelfKillsCheckBox.VerticalAlignment = VerticalAlignment.Center;
            allowSelfKillsCheckBox.IsChecked = _app.Config.FragCutAllowSelfKills;
            allowSelfKillsCheckBox.ToolTip = "Self kills are suicides where the player shot himself with a weapon";

            var allowTeamKillsCheckBox = new CheckBox();
            _allowTeamKillsCheckBox = allowTeamKillsCheckBox;
            allowTeamKillsCheckBox.HorizontalAlignment = HorizontalAlignment.Left;
            allowTeamKillsCheckBox.VerticalAlignment = VerticalAlignment.Center;
            allowTeamKillsCheckBox.IsChecked = _app.Config.FragCutAllowTeamKills;

            var allowAnyDeathCheckBox = new CheckBox();
            _allowAnyDeathCheckBox = allowAnyDeathCheckBox;
            allowAnyDeathCheckBox.HorizontalAlignment = HorizontalAlignment.Left;
            allowAnyDeathCheckBox.VerticalAlignment = VerticalAlignment.Center;
            allowAnyDeathCheckBox.IsChecked = _app.Config.FragCutAllowAnyDeath;
            allowAnyDeathCheckBox.ToolTip = "This includes suicides where the killer is the world: lava, fall damage, hurt triggers, etc";

            var rulesPanelList = new List<Tuple<FrameworkElement, FrameworkElement>>();
            rulesPanelList.Add(App.CreateTuple("Min. Frag Count", minFragCountEditBox));
            rulesPanelList.Add(App.CreateTuple("Time Between Frags", timeBetweenFragsEditBox));
            rulesPanelList.Add(App.CreateTuple("Allow Self Kills?", allowSelfKillsCheckBox));
            rulesPanelList.Add(App.CreateTuple("Allow Any Death?", allowAnyDeathCheckBox));
            rulesPanelList.Add(App.CreateTuple("Allow Team Kills?", allowTeamKillsCheckBox));
            
            var rulesPanel = WpfHelper.CreateDualColumnPanel(rulesPanelList, 120, 5);
            rulesPanel.HorizontalAlignment = HorizontalAlignment.Center;
            rulesPanel.VerticalAlignment = VerticalAlignment.Center;

            var rulesGroupBox = new GroupBox();
            rulesGroupBox.Header = "Frag Rules";
            rulesGroupBox.HorizontalAlignment = HorizontalAlignment.Stretch;
            rulesGroupBox.VerticalAlignment = VerticalAlignment.Stretch;
            rulesGroupBox.Margin = new Thickness(5);
            rulesGroupBox.Content = rulesPanel;

            var autoPlayerSelectionRadioButton = new RadioButton();
            _autoPlayerSelectionRadioButton = autoPlayerSelectionRadioButton;
            autoPlayerSelectionRadioButton.GroupName = "PlayerSelection";
            autoPlayerSelectionRadioButton.Content = " Recording Player";
            autoPlayerSelectionRadioButton.ToolTip = "The player who recorded the demo";
            autoPlayerSelectionRadioButton.IsChecked = true;

            var manualPlayerSelectionRadioButton = new RadioButton();
            _manualPlayerSelectionRadioButton = manualPlayerSelectionRadioButton;
            manualPlayerSelectionRadioButton.GroupName = "PlayerSelection";
            manualPlayerSelectionRadioButton.Content = " This Player";
            manualPlayerSelectionRadioButton.IsChecked = false;

            var playerIndexTextBox = new TextBox();
            _playerIndexTextBox = playerIndexTextBox;
            playerIndexTextBox.Margin = new Thickness(5, 0, 0, 0);
            playerIndexTextBox.Width = 40;
            playerIndexTextBox.ToolTip = "Valid range: [0;63]";

            var manualPlayerSelectionRow = new StackPanel();
            manualPlayerSelectionRow.Margin = new Thickness(0, 5, 0, 0);
            manualPlayerSelectionRow.Orientation = Orientation.Horizontal;
            manualPlayerSelectionRow.HorizontalAlignment = HorizontalAlignment.Stretch;
            manualPlayerSelectionRow.VerticalAlignment = VerticalAlignment.Stretch;
            manualPlayerSelectionRow.Children.Add(manualPlayerSelectionRadioButton);
            manualPlayerSelectionRow.Children.Add(playerIndexTextBox);

            var playerSelectionPanel = new StackPanel();
            playerSelectionPanel.Margin = new Thickness(10);
            playerSelectionPanel.HorizontalAlignment = HorizontalAlignment.Stretch;
            playerSelectionPanel.VerticalAlignment = VerticalAlignment.Stretch;
            playerSelectionPanel.Children.Add(autoPlayerSelectionRadioButton);
            playerSelectionPanel.Children.Add(manualPlayerSelectionRow);

            var playerSelectionGroupBox = new GroupBox();
            playerSelectionGroupBox.Margin = new Thickness(5);
            playerSelectionGroupBox.Header = "Player Selection";
            playerSelectionGroupBox.HorizontalAlignment = HorizontalAlignment.Left;
            playerSelectionGroupBox.VerticalAlignment = VerticalAlignment.Top;
            playerSelectionGroupBox.Content = playerSelectionPanel;

            var cutButton = new Button();
            cutButton.HorizontalAlignment = HorizontalAlignment.Left;
            cutButton.VerticalAlignment = VerticalAlignment.Top;
            cutButton.Content = "Cut!";
            cutButton.Width = 75;
            cutButton.Height = 25;
            cutButton.Margin = new Thickness(5);
            cutButton.Click += (obj, args) => OnCutByFragClicked();

            var actionsGroupBox = new GroupBox();
            actionsGroupBox.HorizontalAlignment = HorizontalAlignment.Left;
            actionsGroupBox.VerticalAlignment = VerticalAlignment.Top;
            actionsGroupBox.Margin = new Thickness(5);
            actionsGroupBox.Header = "Actions";
            actionsGroupBox.Content = cutButton;

            var startTimeOffsetEditBox = new TextBox();
            _startTimeOffsetEditBox = startTimeOffsetEditBox;
            startTimeOffsetEditBox.Width = 40;
            startTimeOffsetEditBox.Text = _app.Config.FragCutStartOffset.ToString();
            startTimeOffsetEditBox.ToolTip = "How many seconds before the first frag in the sequence do we start the cut?";

            var endTimeOffsetEditBox = new TextBox();
            _endTimeOffsetEditBox = endTimeOffsetEditBox;
            endTimeOffsetEditBox.Width = 40;
            endTimeOffsetEditBox.Text = _app.Config.FragCutEndOffset.ToString();
            endTimeOffsetEditBox.ToolTip = "How many seconds after the last frag in the sequence do we end the cut?";

            var timeOffsetPanelList = new List<Tuple<FrameworkElement, FrameworkElement>>();
            timeOffsetPanelList.Add(App.CreateTuple("Start Time Offset", startTimeOffsetEditBox));
            timeOffsetPanelList.Add(App.CreateTuple("End Time Offset", endTimeOffsetEditBox));
            var timeOffsetPanel = WpfHelper.CreateDualColumnPanel(timeOffsetPanelList, 100, 5);
            timeOffsetPanel.HorizontalAlignment = HorizontalAlignment.Center;
            timeOffsetPanel.VerticalAlignment = VerticalAlignment.Center;
            
            var timeOffsetsGroupBox = new GroupBox();
            timeOffsetsGroupBox.Header = "Time Offsets";
            timeOffsetsGroupBox.HorizontalAlignment = HorizontalAlignment.Left;
            timeOffsetsGroupBox.VerticalAlignment = VerticalAlignment.Top;
            timeOffsetsGroupBox.Margin = new Thickness(5);
            timeOffsetsGroupBox.Content = timeOffsetPanel;
            
            var helpTextBlock = new TextBlock();
            helpTextBlock.Margin = new Thickness(5);
            helpTextBlock.TextWrapping = TextWrapping.WrapWithOverflow;
            helpTextBlock.Text = 
                "Self-kills are suicides where the player shot himself with a weapon." +
                "\nDeaths can include 'suicides' where the killer is the world: lava, fall damage, hurt triggers, etc.";
            
            var helpGroupBox = new GroupBox();
            helpGroupBox.Margin = new Thickness(5);
            helpGroupBox.Header = "Help";
            helpGroupBox.Content = helpTextBlock;
            
            var rootPanel = new WrapPanel();
            rootPanel.HorizontalAlignment = HorizontalAlignment.Stretch;
            rootPanel.VerticalAlignment = VerticalAlignment.Stretch;
            rootPanel.Margin = new Thickness(5);
            rootPanel.Orientation = Orientation.Horizontal;
            rootPanel.Children.Add(rulesGroupBox);
            rootPanel.Children.Add(timeOffsetsGroupBox);
            rootPanel.Children.Add(playerSelectionGroupBox);
            rootPanel.Children.Add(actionsGroupBox);
            rootPanel.Children.Add(helpGroupBox);

            var scrollViewer = new ScrollViewer();
            scrollViewer.HorizontalAlignment = HorizontalAlignment.Stretch;
            scrollViewer.VerticalAlignment = VerticalAlignment.Stretch;
            scrollViewer.Margin = new Thickness(5);
            scrollViewer.HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled;
            scrollViewer.VerticalScrollBarVisibility = ScrollBarVisibility.Auto;
            scrollViewer.Content = rootPanel;

            return scrollViewer; 
        }

        private class ThreadArg
        {
            public List<string> FilePaths = null;
            public int PlayerIndex = -1;
        }

        private void OnCutByFragClicked()
        {
            var demos = _app.SelectedDemos;
            if(demos == null)
            {
                _app.LogError("No demo was selected. Please select one to proceed.");
                return;
            }

            var playerIndex = -1;
            var manualMode = _manualPlayerSelectionRadioButton.IsChecked ?? false;
            if(manualMode)
            {
                if(!int.TryParse(_playerIndexTextBox.Text, out playerIndex))
                {
                    _app.LogError("Invalid player index: {0}. Valid range: [0;63]", _playerIndexTextBox.Text);
                    return;
                }
            }

            _app.SaveConfig();
            _app.DisableUiNonThreadSafe();
            _app.JoinJobThread();

            var filePaths = new List<string>();
            foreach(var demo in demos)
            {
                filePaths.Add(demo.FilePath);
            }

            var arg = new ThreadArg();
            arg.FilePaths = filePaths;
            arg.PlayerIndex = playerIndex;

            _app.StartJobThread(DemoCutByFragThread, filePaths);
        }

        private void DemoCutByFragThread(object arg)
        {
            try
            {
                DemoCutByFragThreadImpl(arg);
            }
            catch(Exception exception)
            {
                EntryPoint.RaiseException(exception);
            }
        }

        private void DemoCutByFragThreadImpl(object arg)
        {
            var threadArg = arg as ThreadArg;
            if(threadArg == null)
            {
                return;
            }

            var filePaths = threadArg.FilePaths;
            if(filePaths == null)
            {
                return;
            }

            var outputFolder = _app.GetOutputFolder();
            var outputFolderPtr = Marshal.StringToHGlobalAnsi(outputFolder);
            _app.InitParseArg();
            _app.ParseArg.OutputFolderPath = outputFolderPtr;

            var config = _app.Config;
            UInt32 flags = 0;
            if(config.FragCutAllowAnyDeath)
            {
                flags |= (UInt32)UDT_DLL.udtCutByFragArgFlags.AllowDeaths;
            }
            if(config.FragCutAllowSelfKills)
            {
                flags |= (UInt32)UDT_DLL.udtCutByFragArgFlags.AllowSelfKills;
            }
            if(config.FragCutAllowTeamKills)
            {
                flags |= (UInt32)UDT_DLL.udtCutByFragArgFlags.AllowTeamKills;
            }

            try
            {
                var rules = new UDT_DLL.udtCutByFragArg();
                rules.MinFragCount = (UInt32)config.FragCutMinFragCount;
                rules.TimeBetweenFragsSec = (UInt32)config.FragCutTimeBetweenFrags;
                rules.TimeMode = 0; // @TODO:
                rules.StartOffsetSec = (UInt32)config.ChatCutStartOffset;
                rules.EndOffsetSec = (UInt32)config.ChatCutEndOffset;
                rules.Flags = flags;
                rules.PlayerIndex = (Int32)threadArg.PlayerIndex;
                UDT_DLL.CutDemosByFrag(ref _app.ParseArg, filePaths, rules, config.MaxThreadCount);
            }
            catch(Exception exception)
            {
                _app.LogError("Caught an exception while cutting demos: {0}", exception.Message);
            }

            Marshal.FreeHGlobal(outputFolderPtr);
            _app.EnableUiThreadSafe();
        }
    }
}