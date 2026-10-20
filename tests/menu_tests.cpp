// Library carousel motion, Move gestures, playlist shelves and sound effect
// mixing.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "library_shelf.hpp"
#include "menu_font.hpp"
#include "menu_motion.hpp"
#include "sound_effects.hpp"

using namespace movebeat::ps3;

namespace {

int failures = 0;
const bool verbose = std::getenv("MOVEBEAT_TEST_VERBOSE") != 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::printf("FAIL: %s\n", message);
        ++failures;
    } else if (verbose) {
        std::printf("ok: %s\n", message);
    }
}

// Runs the carousel at 60 Hz until it rests (or a time limit).
float settle(CarouselScroll& scroll, float limit_seconds = 5.0f) {
    float elapsed = 0.0f;
    while (!scroll.resting() && elapsed < limit_seconds) {
        scroll.update(1.0f / 60.0f);
        elapsed += 1.0f / 60.0f;
    }
    return elapsed;
}

void testCarouselSteps() {
    CarouselScroll scroll;
    scroll.reset(5);
    check(scroll.selected(30) == 5 && scroll.resting(), "carousel starts on its row");
    scroll.step(1);
    scroll.update(1.0f / 60.0f);
    check(!scroll.resting() && scroll.position() > 5.0f && scroll.position() < 6.0f,
          "a step animates instead of jumping");
    const float time = settle(scroll);
    check(scroll.position() == 6.0f && time < 0.6f, "a step settles quickly on the next row");
    scroll.step(-3);
    settle(scroll);
    check(scroll.selected(30) == 3, "steps accumulate");
    scroll.reset(0);
    scroll.step(-1);
    settle(scroll);
    check(scroll.selected(30) == 29, "the carousel wraps around the library");
}

void testCarouselInertia() {
    CarouselScroll scroll;
    scroll.reset(0);
    scroll.beginDrag();
    scroll.drag(2.4f);
    check(scroll.position() == 2.4f && scroll.dragging(), "a drag moves the list directly");
    scroll.release(20.0f);
    scroll.update(1.0f / 60.0f);
    const float after_one = scroll.position();
    settle(scroll);
    const float landed = scroll.position();
    check(after_one > 2.4f, "a flick keeps moving after release");
    check(landed == std::floor(landed) && landed > 5.0f && landed < 12.0f,
          "inertia slows down and lands exactly on a row");
    scroll.reset(0);
    scroll.beginDrag();
    scroll.drag(0.4f);
    scroll.release(0.0f);
    settle(scroll);
    check(scroll.position() == 0.0f, "a short drag without a flick snaps back");
    scroll.reset(0);
    scroll.beginDrag();
    scroll.release(500.0f);
    settle(scroll, 10.0f);
    check(scroll.position() < 20.0f, "flick speed is capped");
}

void testRandomizerSpin() {
    CarouselScroll scroll;
    scroll.reset(4);
    MenuRandom random(1234u);
    bool always_other = true;
    for (int trial = 0; trial < 200; ++trial) {
        const std::size_t pick = random.pickOther(7, 30);
        if (pick == 7 || pick >= 30) always_other = false;
    }
    check(always_other, "randomizer always picks a different track");
    check(random.pickOther(0, 1) == 0, "a one-track library stays put");
    // Spin one extra lap then land on the pick.
    scroll.step(30 + 9);
    const float time = settle(scroll);
    check(scroll.selected(30) == 13 && time < 2.5f && time > 1.0f,
          "a randomizer spin takes a visible but short time");
}

void testGestureVertical() {
    MenuGesture gesture;
    MenuGestureEvents events = gesture.update(true, true, 0.0f, 0.0f, 0);
    check(!events.drag_started && events.drag_rows == 0.0f, "pressing T alone does nothing");
    events = gesture.update(true, true, 0.002f, 0.02f, 16);
    check(!events.drag_started, "small jitter does not lock an axis");
    events = gesture.update(true, true, 0.004f, 0.05f, 33);
    check(events.drag_started && gesture.vertical() &&
          std::fabs(events.drag_rows - 0.05f * MenuGesture::rowsPerMeter()) < 1e-4f,
          "a vertical stroke starts a drag that follows the hand");
    float rows = 0.0f;
    for (int frame = 3; frame <= 8; ++frame) {
        events = gesture.update(true, true, 0.004f, 0.05f + (frame - 2) * 0.03f,
                                static_cast<unsigned int>(frame * 16));
        rows += events.drag_rows;
        check(events.difficulty_step == 0, "a vertical stroke never changes difficulty");
    }
    check(std::fabs(rows - 0.18f * MenuGesture::rowsPerMeter()) < 1e-3f,
          "drag rows track the hand's vertical travel");
    events = gesture.update(false, true, 0.004f, 0.23f, 144);
    check(events.released && events.flick_rows_per_second > 15.0f,
          "releasing T mid-motion flicks with the hand's speed");
    // A brief dropout is skipped; a longer loss ends the drag without
    // throwing the list.
    gesture.update(true, true, 0.0f, 0.0f, 200);
    gesture.update(true, true, 0.0f, 0.06f, 216);
    events = gesture.update(true, false, 0.0f, 0.4f, 232);
    check(!events.released && gesture.vertical(), "a one-frame dropout keeps the drag");
    events = gesture.update(true, true, 0.0f, 0.09f, 248);
    check(events.drag_rows > 0.0f, "the drag goes on after the dropout");
    events = gesture.update(true, false, 0.0f, 0.4f, 600);
    check(events.released && events.flick_rows_per_second == 0.0f,
          "lost tracking cancels the flick");
}

void testGestureHorizontal() {
    MenuGesture gesture;
    gesture.update(true, true, 0.0f, 0.0f, 0);
    MenuGestureEvents events = gesture.update(true, true, 0.05f, 0.005f, 16);
    check(gesture.horizontal() && events.drag_rows == 0.0f,
          "a sideways stroke locks to difficulty, not scrolling");
    int steps = 0;
    for (int frame = 2; frame <= 20; ++frame) {
        events = gesture.update(true, true, 0.05f + frame * 0.02f, 0.01f,
                                static_cast<unsigned int>(frame * 16));
        steps += events.difficulty_step;
        check(events.drag_rows == 0.0f, "a sideways stroke never scrolls");
    }
    check(steps == 2, "each 15 cm to the right is one harder step");
    for (int frame = 21; frame <= 40; ++frame) {
        events = gesture.update(true, true, 0.45f - (frame - 20) * 0.025f, 0.01f,
                                static_cast<unsigned int>(frame * 16));
        steps += events.difficulty_step;
    }
    check(steps < 2, "moving back to the left steps easier again");
    events = gesture.update(false, true, 0.0f, 0.0f, 700);
    check(!events.released, "ending a sideways stroke does not flick the list");
}

// Handle fixed in space, the Move only turned by `degrees` up or sideways.
movebeat::SaberPose turned(float pitch_degrees, float yaw_degrees) {
    const float pitch = pitch_degrees * 0.01745329f;
    const float yaw = yaw_degrees * 0.01745329f;
    // Pointing forward (+z) at 0/0; pitch turns up (+y), yaw turns right (+x).
    const movebeat::Vec3 direction(std::sin(yaw) * std::cos(pitch),
                                   std::sin(pitch),
                                   std::cos(yaw) * std::cos(pitch));
    return movebeat::SaberPose(movebeat::Vec3(0.1f, 0.2f, 0.5f),
                               movebeat::Vec3(0.1f + direction.x * 0.9f,
                                              0.2f + direction.y * 0.9f,
                                              0.5f + direction.z * 0.9f),
                               true);
}

void testWristTurnsDriveGestures() {
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
    check(gesturePoint(turned(0.0f, 0.0f), x0, y0) &&
          gesturePoint(turned(10.0f, 0.0f), x1, y1) &&
          std::fabs((y1 - y0) - 0.06f) < 0.005f && std::fabs(x1 - x0) < 1e-4f,
          "tilting the Move up 10 degrees moves the gesture point 6 cm up");
    check(!gesturePoint(movebeat::SaberPose(), x0, y0),
          "an untracked Move has no gesture point");
    // Tilt up from 0 to 40 degrees with T held: a vertical drag, then a flick.
    MenuGesture gesture;
    float rows = 0.0f;
    bool started = false;
    for (int frame = 0; frame <= 10; ++frame) {
        float x = 0.0f, y = 0.0f;
        gesturePoint(turned(frame * 4.0f, 0.0f), x, y);
        const MenuGestureEvents events = gesture.update(
            true, true, x, y, static_cast<unsigned int>(frame * 16));
        started = started || events.drag_started;
        rows += events.drag_rows;
    }
    check(started && gesture.vertical() && rows > 2.0f,
          "a wrist tilt alone scrolls the carousel");
    // Turn right 0 to 60 degrees: difficulty steps, no scrolling.
    MenuGesture sideways;
    int steps = 0;
    float sideways_rows = 0.0f;
    for (int frame = 0; frame <= 15; ++frame) {
        float x = 0.0f, y = 0.0f;
        gesturePoint(turned(0.0f, frame * 4.0f), x, y);
        const MenuGestureEvents events = sideways.update(
            true, true, x, y, static_cast<unsigned int>(frame * 16));
        steps += events.difficulty_step;
        sideways_rows += events.drag_rows;
    }
    check(sideways.horizontal() && steps >= 1 && sideways_rows == 0.0f,
          "turning the Move sideways changes difficulty without scrolling");
}

void testDifficultyColors() {
    check(difficultyRank("Easy") == 0 && difficultyRank("ExpertPlus") == 4 &&
          difficultyRank("Expert+") == 4 && difficultyRank("expert plus") == 4 &&
          difficultyRank("Hard") == 2 && difficultyRank("Unknown") == 2,
          "difficulty names map to ranks");
    const std::uint32_t easy = difficultyColor("Easy");
    const std::uint32_t expert_plus = difficultyColor("ExpertPlus");
    check(((easy >> 8) & 255u) > ((easy >> 16) & 255u) &&
          ((expert_plus >> 16) & 255u) > ((expert_plus >> 8) & 255u) * 3,
          "halo runs from green (easy) to red (expert+)");
}

void testSoundBankAndMixer() {
    SoundBank bank;
    bank.generate(48000);
    bool lengths = true;
    bool bounded = true;
    for (int effect = 0; effect < SFX_COUNT; ++effect) {
        const std::vector<float>& sound = bank.sound(static_cast<SoundEffect>(effect));
        if (sound.empty() || sound.size() > 48000u / 4u) lengths = false;
        for (std::size_t index = 0; index < sound.size(); ++index)
            if (!(std::fabs(sound[index]) <= 0.61f)) bounded = false;
    }
    check(lengths, "every effect is short (under a quarter second)");
    check(bounded, "effects leave headroom for the music");
    check(std::fabs(bank.sound(SFX_TICK)[0]) < 0.05f,
          "effects start softly instead of with a click");

    EffectMixer mixer;
    mixer.attach(&bank);
    std::vector<float> block(256 * 2, 0.0f);
    mixer.mix(&block[0], 256);
    bool silent = true;
    for (std::size_t index = 0; index < block.size(); ++index)
        if (block[index] != 0.0f) silent = false;
    check(silent, "an idle mixer adds nothing");
    check(mixer.play(SFX_CUT, 1.0f, -1.0f), "an effect can be queued");
    mixer.mix(&block[0], 256);
    float left = 0.0f, right = 0.0f;
    for (unsigned int frame = 0; frame < 256; ++frame) {
        left += std::fabs(block[frame * 2]);
        right += std::fabs(block[frame * 2 + 1]);
    }
    check(left > 1.0f && right < left * 0.01f, "pan -1 plays on the left only");
    for (int index = 0; index < 40; ++index) mixer.play(SFX_TICK, 1.0f, 0.0f);
    std::vector<float> loud(256 * 2, 0.9f);
    mixer.mix(&loud[0], 256);
    bool clamped = true;
    for (std::size_t index = 0; index < loud.size(); ++index)
        if (loud[index] > 1.0f || loud[index] < -1.0f) clamped = false;
    check(clamped, "mixed output never exceeds full scale");
    check(mixer.playedCount() == 1u + EffectMixer::kQueue,
          "the request queue is bounded and drains in one block");
}

movebeat::LevelManifest levelWith(const char* key, const char* hash,
                                   const char* const* difficulties, int count) {
    movebeat::LevelManifest level;
    level.source_key = key;
    level.source_hash = hash;
    for (int index = 0; index < count; ++index) {
        movebeat::LevelChart chart;
        chart.characteristic = "Standard";
        chart.difficulty = difficulties[index];
        level.charts.push_back(chart);
    }
    return level;
}

void testPlaylistShelf() {
    static const char* const easy_to_expert[] = {"Easy", "Normal", "Hard", "Expert"};
    static const char* const hard_plus[] = {"Hard", "ExpertPlus"};
    const movebeat::LevelManifest alpha = levelWith(
        "a1", "1111111111111111111111111111111111111111", easy_to_expert, 4);
    const movebeat::LevelManifest beta = levelWith("b2", "", hard_plus, 2);
    const movebeat::LevelManifest gamma = levelWith(
        "c3", "3333333333333333333333333333333333333333", easy_to_expert, 4);
    std::vector<const movebeat::LevelManifest*> library;
    library.push_back(&alpha);
    library.push_back(&beta);
    library.push_back(&gamma);

    movebeat::PlaylistManifest playlist;
    movebeat::PlaylistSong song;
    song.hash = "3333333333333333333333333333333333333333";
    playlist.songs.push_back(song);                       // gamma
    song = movebeat::PlaylistSong();
    song.hash = "9999999999999999999999999999999999999999";
    playlist.songs.push_back(song);                       // not in the library
    song = movebeat::PlaylistSong();
    song.key = "B2";
    song.difficulty = "ExpertPlus";
    playlist.songs.push_back(song);                       // beta
    song = movebeat::PlaylistSong();
    song.hash = "1111111111111111111111111111111111111111";
    song.difficulty = "Normal";
    playlist.songs.push_back(song);                       // alpha
    std::vector<std::size_t> songs;
    const std::vector<std::size_t> tracks = resolvePlaylistTracks(playlist, library, songs);
    check(tracks.size() == 3 && tracks[0] == 2 && tracks[1] == 1 && tracks[2] == 0,
          "playlist tracks keep playlist order and skip missing maps");
    check(songs.size() == 3 && songs[0] == 0 && songs[1] == 2 && songs[2] == 3,
          "each playlist track remembers its playlist song");

    check(choosePlaylistChart(playlist.songs[2], beta, 0) == 1,
          "the difficulty a playlist names wins over the preferred one");
    check(choosePlaylistChart(playlist.songs[3], alpha, 3) == 1,
          "a named Normal is played even when Expert was preferred");
    check(choosePlaylistChart(playlist.songs[0], alpha, 3) == 3,
          "without a suggestion the preferred difficulty is used");
    check(choosePlaylistChart(playlist.songs[0], beta, 3) == 0,
          "an unavailable preferred difficulty falls back to the easier neighbour");
    check(choosePlaylistChart(playlist.songs[0], beta, 4) == 1,
          "an exact preferred rank is found");
    check(choosePlaylistChart(playlist.songs[0], alpha, -1) == 0,
          "no suggestion and no preference keep the first chart");
    movebeat::PlaylistSong missing_named;
    missing_named.difficulty = "Expert";
    check(choosePlaylistChart(missing_named, beta, -1) == 0,
          "a named difficulty the level lacks picks the nearest easier one");

    PlaylistRun run;
    check(!run.active() && !run.hasNext(), "a new run is idle");
    run.begin(0, tracks, 1, 2);
    check(run.active() && run.position() == 1 && run.currentLevel() == 1 &&
          run.preferredRank() == 2, "a run starts on the chosen song");
    PlaylistSongResult first;
    first.level = 1;
    first.score = 1000;
    first.hits = 10;
    first.misses = 2;
    run.record(first);
    check(run.hasNext() && !run.complete(), "a song with one after it is not the end");
    check(run.advance() == 0 && run.position() == 2, "the run advances in playlist order");
    PlaylistSongResult second;
    second.level = 0;
    second.score = 500;
    second.hits = 5;
    second.failed = true;
    run.record(second);
    check(!run.hasNext() && run.complete(), "the last song completes the run");
    run.dropLast();
    check(!run.complete() && run.results().size() == 1, "a replayed song drops its result");
    run.record(second);
    check(run.totalScore() == 1500 && run.totalHits() == 15 && run.totalMisses() == 2,
          "the run totals every song");
    run.stop();
    check(!run.active() && !run.complete(), "a stopped run is idle");
    run.begin(0, tracks, 7, -1);
    check(!run.active(), "a start beyond the playlist does not begin a run");
}

} // namespace

// Also linked into the PPU test package (see game_tests.cpp).
void testMenuFont() {
    // "Ё" and "я" are two-byte UTF-8 letters; "я" draws as "Я".
    const char* text = "Ёя";
    const MenuGlyph yo = nextMenuGlyph(text);
    const MenuGlyph ya = nextMenuGlyph(text);
    const char* capital = "Я";
    check(yo.rows && ya.rows && *text == 0, "Cyrillic letters decode from UTF-8");
    check(ya.rows == nextMenuGlyph(capital).rows, "lowercase Cyrillic draws as capitals");
    check(menuTextWidth("AB", 2) == 24 && menuTextWidth("ПРИВЕТ", 1) == 36,
          "a Cyrillic letter is as wide as a Latin one");
    check(menuTextWidth(ICON_CROSS "  OK", 1) == 9 + 6 * 4 &&
          menuTextWidth(ICON_MOVE, 1) == 9,
          "a button icon is nine pixels wide");
    const char* oval = ICON_MOVE;
    const MenuGlyph move = nextMenuGlyph(oval);
    check(move.height == 11 && move.top == 2, "the Move oval reaches above and below the line");
    const char* icon = ICON_TRIANGLE;
    check(nextMenuGlyph(icon).icon_color != 0, "icons keep their own colors");
    const char* digit = "7";
    check(nextMenuGlyph(digit).rows != 0, "menu text draws digits");
    const char* unknown = "\xe2\x86\x92";  // an arrow: a blank, one letter wide
    const MenuGlyph blank = nextMenuGlyph(unknown);
    check(!blank.rows && blank.advance == 6 && *unknown == 0,
          "a character without a glyph is one blank letter");

    setLanguage(LANGUAGE_EN);
    check(difficultyLabel("ExpertPlus") == "EXPERT+" && std::string(tr("A", "Б")) == "A",
          "English labels");
    setLanguage(LANGUAGE_RU);
    check(difficultyLabel("Easy") == "EASY" && difficultyLabel("ExpertPlus") == "EXPERT+" &&
          std::string(tr("A", "Б")) == "Б",
          "Russian text keeps the English difficulty names");
    setLanguage(LANGUAGE_EN);
}

int runMenuTests() {
    testMenuFont();
    testCarouselSteps();
    testCarouselInertia();
    testRandomizerSpin();
    testGestureVertical();
    testGestureHorizontal();
    testWristTurnsDriveGestures();
    testDifficultyColors();
    testSoundBankAndMixer();
    testPlaylistShelf();
    if (failures) {
        std::printf("%d menu check(s) failed\n", failures);
        return failures;
    }
    std::printf("All menu tests passed.\n");
    return 0;
}

#if !defined(__PPU__)
int main() {
    return runMenuTests() == 0 ? 0 : 1;
}
#endif
