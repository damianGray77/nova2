param([long]$From = 10000000, [long]$Step = 1000000, [string]$Dir = (Join-Path $PSScriptRoot '..\..\runs\open-2\records'), [string]$File = '',
      [ValidateSet('auto', 'gate3', 'gate5')][string]$Layout = 'auto')
# Summarise a run's binary samples (<seed>.samples) per window of $Step ticks. Shared read only.
# Two row layouts, each the Sample struct (core/include/nova2/observe.h) of the build that wrote it:
# - gate3: 51 values a row (408 bytes), open-2's build. Six death causes.
# - gate5: 138 values a row (1,104 bytes), gate 5's build (open-3). It adds three death causes (heat, oom, watchdog),
#   gate 4's power and heat values, and gate 5's calls, injections and failures. Values that moved since gate 3:
#   deaths_other / cross_owner / region_ratio are where gate 3 had d_cut / cross / region, as the CSV shows them.
#   Index 21 (region_ratio) is a double; 57-62 (power and temperature) are signed.
# The output's columns are the same measures under both layouts. gate5 has every death cause in its binary rows
# (the exe's CSV sums the last seven into deaths_other), so its deaths part lists nine causes, the last three
# being heat, oom and watchdog, which gate 3 did not have.
# Under gate5 the network part is window totals (sent, delivered, pings answered, full, stranded, cut), since the counts
# are small. Its "full" counts drops of every kind of message (data, PING requests, echoes) and "sent" only data
# messages, so full can exceed sent; gate 3's "full%" (full over sent) is kept as it was for open-2.
# -Layout auto picks the layout from the file: the one whose second and third rows' ticks are twice and three
# times the first row's (rows are written at a fixed number of ticks apart). That still works when the file's
# length divides by both widths, or ends in a part-written row. -Layout gate3 or gate5 forces one.
$src = @'
using System;
using System.IO;
using System.Text;
public static class Samp {
    // The positions of the measures the summary uses, per layout.
    class L {
        public string name; public int w; public int region;
        public int deaths, ndeaths, cross, hijack, scav, genomes, lineages, cold, boots, sent, deliv, pings, full;
        public int colonies, colAlive, colonists;
        public string[] nm;
    }
    static L Gate3() {
        var l = new L { name = "gate3", w = 51, region = 18, deaths = 6, ndeaths = 6, cross = 19, hijack = 20, scav = 21,
            genomes = 22, lineages = 24, cold = 37, boots = 38, sent = 39, deliv = 40, pings = 41, full = 42,
            colonies = 46, colAlive = 47, colonists = 49 };
        l.nm = new string[] {"tick","programs","threads","started","births","faithful","d_exit","d_fault","d_cut","d_dev","d_memlost","d_cold",
              "idle_runs","idle_faults","idle_caps","paths_new","idle_sends","instr","region","cross","hijack","scav","genomes","groups","lineages",
              "written","region_c","program_c","near_c","image_c","handovers","r_alloc","r_alloc_ok","r_spawn","r_spawn_ok","r_cpy","r_cpy_own",
              "cold","boots","sent","deliv","pings","full","strand","lost","cut","colonies","col_alive","settled","colonists","held"};
        return l;
    }
    static L Gate5() {
        var l = new L { name = "gate5", w = 138, region = 21, deaths = 6, ndeaths = 9, cross = 22, hijack = 23, scav = 24,
            genomes = 25, lineages = 27, cold = 40, boots = 41, sent = 42, deliv = 43, pings = 44, full = 45,
            colonies = 49, colAlive = 50, colonists = 52 };
        var n = new System.Collections.Generic.List<string>(new string[] {"tick","programs","threads","started","births","faithful",
              "d_exit","d_fault","d_cut","d_dev","d_memlost","d_cold","d_heat","d_oom","d_watchdog",
              "idle_runs","idle_faults","idle_caps","paths_new","idle_sends","instr","region","cross","hijack","scav","genomes","groups","lineages",
              "written","region_c","program_c","near_c","image_c","handovers","r_alloc","r_alloc_ok","r_spawn","r_spawn_ok","r_cpy","r_cpy_own",
              "cold","boots","sent","deliv","pings","full","strand","lost","cut","colonies","col_alive","settled","colonists","held",
              "sources","sources_swell","sources_out","power_given","power_drawn","power_lost","heat_out","temp_mean","temp_high",
              "trips_p","trips_e","trips_memory","trips_disk","trips_network","trips_cooler","cutouts",
              "lv_handlers","lv_raised","lv_taken","lv_slept","lv_outlived","solves","solve_sweeps","solves_capped"});
        string[] calls = {"alloc","free","protect","spawn","exit","sleep","setclock","setcore","sethandler","hret","sense","send","recv","dread","dwrite","ping"};
        foreach (var c in calls) n.Add("calls_" + c);
        foreach (var c in calls) n.Add("ok_" + c);
        n.AddRange(new string[] {"sends_other","sends_other_ok","spawns_thread","spawns_thread_ok","spawns_program","spawns_program_ok",
              "recvs","overruns","over_own","over_kin","over_other","over_stack","over_free","overruns_unknown","reader_births","injection_births",
              "readers","reader_programs","injections","failures","replaced","visits","respawns","link_cuts","link_worn","mended","strikes","partings"});
        l.nm = n.ToArray();
        if (l.nm.Length != l.w) throw new Exception("gate5 names: " + l.nm.Length);
        return l;
    }
    static int W;
    static long[] Row(FileStream f, long i) {
        int nb = W * 8; var b = new byte[nb]; f.Seek(i * nb, SeekOrigin.Begin);
        int got = 0; while (got < nb) { int k = f.Read(b, got, nb - got); if (k <= 0) break; got += k; }
        var r = new long[W]; for (int j = 0; j < W; ++j) r[j] = BitConverter.ToInt64(b, j * 8); return r;
    }
    static long Tick(FileStream f, long off) {
        if (off + 8 > f.Length) return -1;
        var b = new byte[8]; f.Seek(off, SeekOrigin.Begin);
        int got = 0; while (got < 8) { int k = f.Read(b, got, 8 - got); if (k <= 0) return -1; got += k; }
        return BitConverter.ToInt64(b, 0);
    }
    // A layout fits if rows 1 and 2 start with ticks twice and three times row 0's.
    static bool Fits(FileStream f, int w) {
        long t0 = Tick(f, 0), t1 = Tick(f, w * 8L), t2 = Tick(f, w * 16L);
        return t0 > 0 && t1 == 2 * t0 && t2 == 3 * t0;
    }
    public static string Run(string path, long from, long step, string layout) {
        var sb = new StringBuilder();
        using (var f = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete, 1 << 16)) {
            L l;
            if (layout == "gate3") l = Gate3();
            else if (layout == "gate5") l = Gate5();
            else {
                bool g3 = Fits(f, 51), g5 = Fits(f, 138);
                if (g3 == g5) throw new Exception("can't tell the row layout (gate3 fits: " + g3 + ", gate5 fits: " + g5 + "); pass -Layout");
                l = g3 ? Gate3() : Gate5();
            }
            W = l.w; bool g5l = l.name == "gate5";
            long n = f.Length / (W * 8); var last = Row(f, n - 1); var first = Row(f, 0);
            long per = last[0] / n; // ticks per row
            if (g5l) sb.AppendFormat("layout gate5 ({0} values a row)\n", W);
            sb.AppendFormat("rows {0} first tick {1} last tick {2} ticks/row {3}\n", n, first[0], last[0], per);
            sb.Append("last row:");
            for (int j = 0; j < W; ++j) {
                if (j == l.region) { if (g5l) sb.AppendFormat(" {0}={1:F4}", l.nm[j], BitConverter.Int64BitsToDouble(last[j])); continue; }
                sb.AppendFormat(" {0}={1}", l.nm[j], last[j]);
            }
            sb.Append("\n");
            sb.AppendLine("window_start  progs_mean threads_mean cross_mean hijack_mean col_alive_mean | births/tick faithful% started/tick started:births | deaths/tick exit fault cut dev memlost cold"
                + (g5l ? " heat oom watchdog" : "") + " | kept/tick lineages/tick scav/tick | cold boots | "
                + (g5l ? "sent deliv pings full_any stranded cut (window totals)" : "sent deliv full% pings") + " colonists colonies");
            int d0 = l.deaths, d1 = l.deaths + l.ndeaths;
            for (long t0 = from; t0 < last[0]; t0 += step) {
                long i0 = t0 / per - 1, i1 = Math.Min((t0 + step) / per - 1, n - 1); if (i0 < 0) i0 = 0;
                var a = Row(f, i0); var z = Row(f, i1); double dt = z[0] - a[0]; if (dt <= 0) continue;
                double sp = 0, sth = 0, sc = 0, sh = 0, sca = 0; int m = 0;
                for (long i = i0; i <= i1; i += Math.Max(1, (i1 - i0) / 200)) { var r = Row(f, i); sp += r[1]; sth += r[2]; sc += r[l.cross]; sh += r[l.hijack]; sca += r[l.colAlive]; ++m; }
                double births = z[4] - a[4], faith = z[5] - a[5], st = z[3] - a[3];
                double dsum = 0; for (int j = d0; j < d1; ++j) dsum += z[j] - a[j];
                sb.AppendFormat("{0,10}-{1,-10} {2,8:F0} {3,9:F0} {4,7:F0} {5,6:F0} {6,5:F1} | {7,6:F1} {8,5:F1} {9,7:F0} {10,5:F1} | {11,6:F1}",
                    a[0], z[0], sp / m, sth / m, sc / m, sh / m, sca / m, births / dt, 100 * faith / Math.Max(1, births), st / dt, st / Math.Max(1, births), dsum / dt);
                for (int j = d0; j < d1; ++j) sb.AppendFormat(" {0:F1}%", 100.0 * (z[j] - a[j]) / Math.Max(1, dsum));
                if (g5l) {
                    // Gate 5's runs send little: window totals, not per tick. "full" counts drops of every kind of
                    // message (data, PING requests and their echoes; net.cpp post and route_on), while "sent" counts
                    // data messages only, so full is not a share of sent.
                    sb.AppendFormat(" | {0:F2} {1:F3} {2:F3} | {3} {4} | {5} {6} {7} {8} {9} {10} {11} {12}\n",
                        (z[l.genomes] - a[l.genomes]) / dt, (z[l.lineages] - a[l.lineages]) / dt, (z[l.scav] - a[l.scav]) / dt, z[l.cold] - a[l.cold], z[l.boots] - a[l.boots],
                        z[l.sent] - a[l.sent], z[l.deliv] - a[l.deliv], z[l.pings] - a[l.pings], z[l.full] - a[l.full], z[l.full + 1] - a[l.full + 1], z[l.full + 3] - a[l.full + 3],
                        z[l.colonists] - a[l.colonists], z[l.colonies] - a[l.colonies]);
                    continue;
                }
                sb.AppendFormat(" | {0:F2} {1:F3} {2:F3} | {3} {4} | {5:F0} {6:F0} {7:F1}% {8:F0} {9} {10}\n",
                    (z[l.genomes] - a[l.genomes]) / dt, (z[l.lineages] - a[l.lineages]) / dt, (z[l.scav] - a[l.scav]) / dt, z[l.cold] - a[l.cold], z[l.boots] - a[l.boots],
                    (z[l.sent] - a[l.sent]) / dt, (z[l.deliv] - a[l.deliv]) / dt, 100.0 * (z[l.full] - a[l.full]) / Math.Max(1, z[l.sent] - a[l.sent]), (z[l.pings] - a[l.pings]) / dt,
                    z[l.colonists] - a[l.colonists], z[l.colonies] - a[l.colonies]);
            }
        }
        return sb.ToString();
    }
}
'@
Add-Type -TypeDefinition $src -Language CSharp
if (-not $File) { $File = (Get-ChildItem -LiteralPath $Dir -Filter *.samples | Select-Object -First 1).FullName }
[Samp]::Run($File, $From, $Step, $Layout)
