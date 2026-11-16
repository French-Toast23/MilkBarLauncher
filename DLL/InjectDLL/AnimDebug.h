#pragma once
// Animation test mode. Enable with "test" in %APPDATA%\BOTWM\anim_mode.txt.
//
// Every remote player ignores the animation it receives and plays the current test animation instead.
// Its name tag shows "<index>/<count> <animation name>". Hotkeys only act while this Cemu window has focus:
//   Ctrl+Shift+Right / Left  next / previous animation
//   Ctrl+Shift+Up            mark OK and go to the next one
//   Ctrl+Shift+Down          mark BAD and go to the next one
//   Ctrl+Shift+R             replay the current animation (plays idle for a moment, then it again)
//   Ctrl+Shift+A             auto-advance every 4 seconds on/off
// Optional %APPDATA%\BOTWM\anim_test_filter.txt: one name fragment per line (e.g. "Sword_", "Climb_");
// only animations whose name contains one of them are tested. Results are appended to
// %APPDATA%\BOTWM\anim_test_results.txt and the position is kept in anim_test_state.txt.
#include <Windows.h>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>
#include "AnimTables.h"
#include "AnimNames.h"

namespace AnimDebug
{
	inline std::string BotwmPath(const char* file)
	{
		char* appdata = nullptr; size_t len = 0;
		std::string p;
		if (_dupenv_s(&appdata, &len, "APPDATA") == 0 && appdata)
		{
			p = std::string(appdata) + "\\BOTWM\\" + file;
			free(appdata);
		}
		return p;
	}

	struct State
	{
		std::mutex Mutex;
		std::vector<uint32_t> List;
		std::string Filter;
		size_t Index = 0;
		bool Built = false;
		bool Auto = false;
		DWORD LastAdvance = 0;
		DWORD ReplayUntil = 0;
		bool KeyDown[6] = {};
	};

	inline State& S() { static State s; return s; }

	inline void Build(State& s)
	{
		std::vector<std::string> filters;
		{
			std::ifstream f(BotwmPath("anim_test_filter.txt"));
			std::string line;
			while (std::getline(f, line))
			{
				while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
				if (!line.empty()) filters.push_back(line);
			}
		}
		for (uint32_t h : AnimTables::ValidAnims)
		{
			if (filters.empty()) { s.List.push_back(h); continue; }
			std::string n = AnimNames::Lookup(h);
			for (const auto& flt : filters)
				if (n.find(flt) != std::string::npos) { s.List.push_back(h); break; }
		}
		if (s.List.empty()) s.List.push_back(AnimTables::IdleAnim);
		for (const auto& flt : filters) s.Filter += (s.Filter.empty() ? "" : ",") + flt;

		std::ifstream st(BotwmPath("anim_test_state.txt"));
		std::string savedFilter; size_t idx = 0;
		if (std::getline(st, savedFilter) && st >> idx && savedFilter == s.Filter && idx < s.List.size())
			s.Index = idx;
		s.Built = true;

		std::stringstream ss;
		ss << "Animation test mode: " << s.List.size() << " animations, filter '" << s.Filter << "', starting at " << s.Index + 1;
		Logging::LoggerService::LogInformation(ss.str(), "AnimTest");
	}

	inline void SaveState(State& s)
	{
		std::ofstream st(BotwmPath("anim_test_state.txt"), std::ios::trunc);
		st << s.Filter << "\n" << s.Index << "\n";
	}

	inline void LogCurrent(State& s)
	{
		uint32_t h = s.List[s.Index];
		std::stringstream ss;
		ss << s.Index + 1 << "/" << s.List.size() << " " << AnimNames::Lookup(h) << " (" << h << ")";
		Logging::LoggerService::LogInformation(ss.str(), "AnimTest");
	}

	inline void Mark(State& s, const char* verdict)
	{
		uint32_t h = s.List[s.Index];
		std::ofstream r(BotwmPath("anim_test_results.txt"), std::ios::app);
		r << verdict << "\t" << h << "\t" << AnimNames::Lookup(h) << "\n";
		Logging::LoggerService::LogInformation(std::string(verdict) + " " + AnimNames::Lookup(h), "AnimTest");
	}

	inline void Step(State& s, int delta)
	{
		size_t n = s.List.size();
		s.Index = (s.Index + n + (delta % (int)n) + n) % n;
		s.LastAdvance = GetTickCount();
		SaveState(s);
		LogCurrent(s);
	}

	inline bool OurWindowFocused()
	{
		HWND fg = GetForegroundWindow();
		DWORD pid = 0;
		if (fg) GetWindowThreadProcessId(fg, &pid);
		return pid == GetCurrentProcessId();
	}

	// Polls hotkeys (edge-triggered) and auto-advance. Cheap; called from every remote player's update.
	inline void Tick()
	{
		State& s = S();
		std::lock_guard<std::mutex> lock(s.Mutex);
		if (!s.Built) { Build(s); LogCurrent(s); s.LastAdvance = GetTickCount(); }

		bool mods = OurWindowFocused() && (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_SHIFT) & 0x8000);
		const int keys[6] = { VK_RIGHT, VK_LEFT, VK_UP, VK_DOWN, 'R', 'A' };
		for (int i = 0; i < 6; i++)
		{
			bool down = mods && (GetAsyncKeyState(keys[i]) & 0x8000);
			bool pressed = down && !s.KeyDown[i];
			s.KeyDown[i] = down;
			if (!pressed) continue;
			switch (i)
			{
			case 0: Step(s, 1); break;
			case 1: Step(s, -1); break;
			case 2: Mark(s, "OK"); Step(s, 1); break;
			case 3: Mark(s, "BAD"); Step(s, 1); break;
			case 4: s.ReplayUntil = GetTickCount() + 300; break;
			case 5:
				s.Auto = !s.Auto; s.LastAdvance = GetTickCount();
				Logging::LoggerService::LogInformation(s.Auto ? "auto-advance on" : "auto-advance off", "AnimTest");
				break;
			}
		}
		if (s.Auto && GetTickCount() - s.LastAdvance > 4000)
			Step(s, 1);
	}

	// Hash to play now (idle during a replay gap, so IsIgnoreSame lets the same animation start again).
	inline uint32_t Current()
	{
		State& s = S();
		std::lock_guard<std::mutex> lock(s.Mutex);
		if (!s.Built) return AnimTables::IdleAnim;
		if (GetTickCount() < s.ReplayUntil && s.List[s.Index] != AnimTables::IdleAnim) return AnimTables::IdleAnim;
		return s.List[s.Index];
	}

	// Name tag text, at most 31 characters.
	inline std::string Label()
	{
		State& s = S();
		std::lock_guard<std::mutex> lock(s.Mutex);
		if (!s.Built) return "anim test";
		std::string l = std::to_string(s.Index + 1) + "/" + std::to_string(s.List.size()) + " " + AnimNames::Lookup(s.List[s.Index]);
		if (s.Auto) l = "> " + l;
		return l.substr(0, 31);
	}
}
