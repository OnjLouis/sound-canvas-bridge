#pragma once
#include "../Source/AppPreferences.h"

namespace preferencesStartupFixture {
template<class Check> void run(Check check) {
    const auto profile=juce::SystemStats::getEnvironmentVariable("SCBRIDGE_PROFILE_DIR",{});
    check(juce::File::isAbsolutePath(profile),"Preference tests require an isolated profile");
    if(!juce::File::isAbsolutePath(profile)) return;
    const auto root=juce::File(profile);
    check(root.createDirectory().wasOk(),"Create isolated preferences directory");
    const auto file=root.getNonexistentChildFile("first-launch-test",".xml");
    {
        scbridge::AppPreferences first(file);
        check(first.isFirstLaunch(),"Missing settings must identify the first launch before updater writes");
        check(first.setStartHidden(true) && first.setFrequency(scbridge::CheckFrequency::hourly),"Persist test preferences");
        scbridge::AppPreferences existing(file);
        check(!existing.isFirstLaunch(),"Existing settings must not trigger first-run setup");
        check(existing.startHidden() && existing.frequency()==scbridge::CheckFrequency::hourly,"Existing choices must be preserved");
        check(scbridge::AppPreferences::shouldShowEngineSetup(true,true),"First launch must show General even with an installed engine");
        check(scbridge::AppPreferences::shouldShowEngineSetup(false,false),"A missing engine must show General");
        check(!scbridge::AppPreferences::shouldShowEngineSetup(false,true),"Existing working users must keep their startup behaviour");
    }
    check(file.deleteFile(),"Remove only the owned preference fixture");
}
}
