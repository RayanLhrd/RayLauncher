// SPDX-License-Identifier: GPL-3.0-only
/*
 *  RayLauncher - Minecraft Launcher
 *  Copyright (C) 2026 RayLauncher Contributors
 */

#include <QTemporaryDir>
#include <QTest>

#include "modplatform/raylauncher/RayModpackManifest.h"

class RayModpackManifestTest : public QObject {
    Q_OBJECT
   private slots:

    // ── Smart-merge: the critical correctness path for options.txt ──────────────────────────

    // The empty-user case is what a fresh install looks like: there's no options.txt yet,
    // so we just emit the canonical verbatim (every key from the pack lands).
    void smartMerge_emptyUser_emitsFullCanonical()
    {
        QByteArray canonical =
            "fov:80\n"
            "key_key.attack:button.left\n"
            "key_key.jump:key.keyboard.space\n";
        QByteArray merged = RayOptionsMerge::smartMerge(QByteArray(), canonical, {}, false);
        auto parsed = RayOptionsMerge::parse(merged);
        QCOMPARE(parsed.values.value("fov"), QStringLiteral("80"));
        QCOMPARE(parsed.values.value("key_key.attack"), QStringLiteral("button.left"));
        QCOMPARE(parsed.values.value("key_key.jump"), QStringLiteral("key.keyboard.space"));
    }

    // User customized a key, pack canonical kept the same. We must NEVER overwrite the user's
    // customization. This is the most important guarantee for player trust.
    void smartMerge_userCustomized_userValueWins()
    {
        QByteArray user =
            "fov:80\n"
            "key_key.attack:button.right\n";  // ← customized
        QByteArray canonical =
            "fov:80\n"
            "key_key.attack:button.left\n";  // canonical is still left
        QHash<QString, QString> snapshot = { { "fov", "80" }, { "key_key.attack", "button.left" } };

        QByteArray merged = RayOptionsMerge::smartMerge(user, canonical, snapshot, false);
        auto parsed = RayOptionsMerge::parse(merged);
        QCOMPARE(parsed.values.value("key_key.attack"), QStringLiteral("button.right"));
    }

    // Pack author bumps a default. The user never touched that key (current value == old
    // canonical). We propagate the new canonical so the bump actually reaches players.
    void smartMerge_userUntouchedKey_picksUpNewCanonical()
    {
        QByteArray user =
            "fov:70\n"           // = old canonical (never touched by user)
            "key_key.jump:key.keyboard.space\n";
        QByteArray newCanonical =
            "fov:90\n"           // bumped
            "key_key.jump:key.keyboard.space\n";
        QHash<QString, QString> oldSnapshot = { { "fov", "70" },
                                                 { "key_key.jump", "key.keyboard.space" } };

        QByteArray merged = RayOptionsMerge::smartMerge(user, newCanonical, oldSnapshot, false);
        auto parsed = RayOptionsMerge::parse(merged);
        QCOMPARE(parsed.values.value("fov"), QStringLiteral("90"));
    }

    // New keybind appears in the pack (e.g., new mod). User doesn't have it. We add it at
    // the end so on next launch Minecraft sees the binding.
    void smartMerge_newCanonicalKey_getsAdded()
    {
        QByteArray user =
            "fov:80\n"
            "key_key.attack:button.left\n";
        QByteArray newCanonical =
            "fov:80\n"
            "key_key.attack:button.left\n"
            "key_key.toggleCrouch:key.keyboard.r\n";  // ← new mod's keybind
        QHash<QString, QString> oldSnapshot = { { "fov", "80" }, { "key_key.attack", "button.left" } };

        QByteArray merged = RayOptionsMerge::smartMerge(user, newCanonical, oldSnapshot, false);
        auto parsed = RayOptionsMerge::parse(merged);
        QCOMPARE(parsed.values.value("key_key.toggleCrouch"), QStringLiteral("key.keyboard.r"));
        // Plus the existing keys should be untouched.
        QCOMPARE(parsed.values.value("fov"), QStringLiteral("80"));
        QCOMPARE(parsed.values.value("key_key.attack"), QStringLiteral("button.left"));
    }

    // Keys ONLY in the user's file (not in canonical) must be preserved. This catches the
    // "user customized something the pack doesn't ship a default for" case.
    void smartMerge_userOnlyKey_preserved()
    {
        QByteArray user = "user_secret_setting:42\nfov:80\n";
        QByteArray canonical = "fov:80\n";
        QByteArray merged = RayOptionsMerge::smartMerge(user, canonical, {}, false);
        auto parsed = RayOptionsMerge::parse(merged);
        QCOMPARE(parsed.values.value("user_secret_setting"), QStringLiteral("42"));
    }

    // The escape hatch from the catalogue's `force_options_reset_for_version`: when active,
    // we replace verbatim with no smart merge at all. Used to recover from a botched export.
    void smartMerge_forceReset_replacesVerbatim()
    {
        QByteArray user =
            "fov:50\n"
            "key_key.attack:button.right\n";
        QByteArray canonical =
            "fov:80\n"
            "key_key.attack:button.left\n";
        QByteArray merged = RayOptionsMerge::smartMerge(user, canonical, {}, /*forceReset=*/true);
        QCOMPARE(merged, canonical);
    }

    // No snapshot (= fresh install of v1.1.0 over a pre-v1.1.0 instance, or first-ever
    // update). We can't tell "untouched" from "customized" so the safe default is "user
    // wins for any existing key". Only new keys land.
    void smartMerge_noSnapshot_userWinsAlways_newKeysLand()
    {
        QByteArray user = "fov:50\nkey_key.attack:button.right\n";
        QByteArray canonical = "fov:80\nkey_key.attack:button.left\nkey_key.newMod:key.keyboard.x\n";
        QByteArray merged = RayOptionsMerge::smartMerge(user, canonical, {}, false);
        auto parsed = RayOptionsMerge::parse(merged);
        QCOMPARE(parsed.values.value("fov"), QStringLiteral("50"));                  // user wins
        QCOMPARE(parsed.values.value("key_key.attack"), QStringLiteral("button.right"));  // user wins
        QCOMPARE(parsed.values.value("key_key.newMod"), QStringLiteral("key.keyboard.x"));  // new lands
    }

    // ── Parsing edge cases ──────────────────────────────────────────────────────────────────

    void parse_bomStripped()
    {
        // 3 bytes BOM + "fov:80\n" (7 bytes) = 10 bytes total. The string literal is split
        // in two so MSVC doesn't greedily consume the 'f' in "fov" as another hex digit of
        // the `\xBF` escape sequence (which would be `\xBFf` → out-of-range byte, hard error
        // under MSVC's C7744). Adjacent string literal concatenation produces the exact same
        // bytes at compile time.
        QByteArray withBom = QByteArray("\xEF\xBB\xBF" "fov:80\n", 10);
        auto parsed = RayOptionsMerge::parse(withBom);
        QCOMPARE(parsed.values.value("fov"), QStringLiteral("80"));
    }

    void parse_crlfNormalized()
    {
        QByteArray crlf = "fov:80\r\nkey_key.attack:button.left\r\n";
        auto parsed = RayOptionsMerge::parse(crlf);
        QCOMPARE(parsed.values.size(), 2);
        QCOMPARE(parsed.values.value("fov"), QStringLiteral("80"));
        QCOMPARE(parsed.values.value("key_key.attack"), QStringLiteral("button.left"));
    }

    // resourcePacks holds a JSON-like list with a colon inside. We must split on the FIRST
    // colon only — otherwise we'd lose half the value.
    void parse_colonInValue_splitOnFirstOnly()
    {
        QByteArray data = "resourcePacks:[\"vanilla\",\"file/pack.zip\"]\n";
        auto parsed = RayOptionsMerge::parse(data);
        QCOMPARE(parsed.values.value("resourcePacks"), QStringLiteral("[\"vanilla\",\"file/pack.zip\"]"));
    }

    void parse_blankAndCommentLines_skipped()
    {
        QByteArray data = "\n# some comment without value\nfov:80\n\n";
        auto parsed = RayOptionsMerge::parse(data);
        QCOMPARE(parsed.values.size(), 1);
        QCOMPARE(parsed.values.value("fov"), QStringLiteral("80"));
    }

    void parse_duplicateKey_lastWins()
    {
        QByteArray data = "fov:80\nfov:90\n";
        auto parsed = RayOptionsMerge::parse(data);
        QCOMPARE(parsed.values.value("fov"), QStringLiteral("90"));
    }

    void parse_whitespaceTrimmed()
    {
        QByteArray data = "  fov : 80  \n";
        auto parsed = RayOptionsMerge::parse(data);
        QCOMPARE(parsed.values.value("fov"), QStringLiteral("80"));
    }

    // ── Manifest serialization ──────────────────────────────────────────────────────────────

    void manifest_roundTrip_filesAndDeps()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        RayModpackManifest m;
        m.modpackId = "zgueg-create";
        m.packVersion = "3.0.1";
        m.minecraftVersion = "1.21.1";
        m.neoforgeVersion = "21.1.228";
        RayManifestFileEntry e1{ "abc123", RayManifestFileEntry::Source::Remote, "https://example.com/mod.jar" };
        RayManifestFileEntry e2{ "def456", RayManifestFileEntry::Source::Override, QString() };
        m.files.insert("mods/foo.jar", e1);
        m.files.insert("config/foo.toml", e2);
        m.optionsCanonicalSnapshot.insert("fov", "80");

        QVERIFY(m.save(tmp.path()));

        auto loaded = RayModpackManifest::load(tmp.path());
        QVERIFY(loaded.has_value());
        QCOMPARE(loaded->modpackId, QStringLiteral("zgueg-create"));
        QCOMPARE(loaded->packVersion, QStringLiteral("3.0.1"));
        QCOMPARE(loaded->minecraftVersion, QStringLiteral("1.21.1"));
        QCOMPARE(loaded->neoforgeVersion, QStringLiteral("21.1.228"));
        QCOMPARE(loaded->files.size(), 2);
        QCOMPARE(loaded->files["mods/foo.jar"].sha512, QStringLiteral("abc123"));
        QCOMPARE(loaded->files["mods/foo.jar"].source, RayManifestFileEntry::Source::Remote);
        QCOMPARE(loaded->files["mods/foo.jar"].remoteUrl, QStringLiteral("https://example.com/mod.jar"));
        QCOMPARE(loaded->files["config/foo.toml"].source, RayManifestFileEntry::Source::Override);
        QCOMPARE(loaded->optionsCanonicalSnapshot.value("fov"), QStringLiteral("80"));
    }

    void manifest_missing_returnsNullopt()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        auto loaded = RayModpackManifest::load(tmp.path());
        QVERIFY(!loaded.has_value());
    }

    void manifest_corruptJson_returnsNullopt()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QFile f(RayModpackManifest::pathFor(tmp.path()));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{this is not valid JSON");
        f.close();
        auto loaded = RayModpackManifest::load(tmp.path());
        QVERIFY(!loaded.has_value());
    }

    // ── Diff computation ────────────────────────────────────────────────────────────────────

    void diff_empty_old_means_all_added()
    {
        RayModpackManifest oldM;
        RayModpackManifest newM;
        newM.files.insert("mods/a.jar", { "h1", RayManifestFileEntry::Source::Remote, "u1" });
        newM.files.insert("mods/b.jar", { "h2", RayManifestFileEntry::Source::Remote, "u2" });

        auto d = RayModpackManifest::diff(oldM, newM);
        QCOMPARE(d.toAdd.size(), 2);
        QCOMPARE(d.toRemove.size(), 0);
        QCOMPARE(d.toModify.size(), 0);
        QCOMPARE(d.unchanged.size(), 0);
    }

    void diff_identical_means_all_unchanged()
    {
        RayModpackManifest m;
        m.files.insert("mods/a.jar", { "h1", RayManifestFileEntry::Source::Remote, "u1" });
        m.files.insert("config/x.toml", { "h2", RayManifestFileEntry::Source::Override, QString() });
        auto d = RayModpackManifest::diff(m, m);
        QCOMPARE(d.toAdd.size(), 0);
        QCOMPARE(d.toRemove.size(), 0);
        QCOMPARE(d.toModify.size(), 0);
        QCOMPARE(d.unchanged.size(), 2);
    }

    void diff_addRemoveModify_classifiedCorrectly()
    {
        RayModpackManifest oldM;
        oldM.files.insert("mods/keep.jar", { "h1", RayManifestFileEntry::Source::Remote, "u1" });
        oldM.files.insert("mods/changed.jar", { "h2", RayManifestFileEntry::Source::Remote, "u2" });
        oldM.files.insert("mods/removed.jar", { "h3", RayManifestFileEntry::Source::Remote, "u3" });

        RayModpackManifest newM;
        newM.files.insert("mods/keep.jar", { "h1", RayManifestFileEntry::Source::Remote, "u1" });
        newM.files.insert("mods/changed.jar", { "h2-new", RayManifestFileEntry::Source::Remote, "u2-new" });
        newM.files.insert("mods/added.jar", { "h4", RayManifestFileEntry::Source::Remote, "u4" });

        auto d = RayModpackManifest::diff(oldM, newM);
        QCOMPARE(d.toAdd, QStringList{ "mods/added.jar" });
        QCOMPARE(d.toRemove, QStringList{ "mods/removed.jar" });
        QCOMPARE(d.toModify, QStringList{ "mods/changed.jar" });
        QCOMPARE(d.unchanged, QStringList{ "mods/keep.jar" });
    }

    void dependenciesMatch_truthTable()
    {
        RayModpackManifest a;
        a.minecraftVersion = "1.21.1";
        a.neoforgeVersion = "21.1.228";

        RayModpackManifest b = a;
        QVERIFY(a.dependenciesMatch(b));

        b.neoforgeVersion = "21.1.229";
        QVERIFY(!a.dependenciesMatch(b));

        b = a;
        b.minecraftVersion = "1.22";
        QVERIFY(!a.dependenciesMatch(b));

        // Empty + empty should match too — useful for the "no old manifest" sentinel.
        RayModpackManifest empty1, empty2;
        QVERIFY(empty1.dependenciesMatch(empty2));
    }
};

QTEST_GUILESS_MAIN(RayModpackManifestTest)

#include "RayModpackManifest_test.moc"
