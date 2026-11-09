// textpath: turns text into glyph outlines with CoreText, so the figures render the same on every machine
// (an SVG shown through <img> cannot load fonts). Reads one JSON object per line on stdin:
//   {"font": "Helvetica-Bold", "text": "SECTION 4"}
// and answers one JSON object per line: glyph ids with their x positions at 1000 units per em (kerning applied),
// the run's width, the font's vertical metrics, and the outline ("d") of every glyph id not sent before.
// Outlines are y-down with the baseline at 0, ready for an SVG <path>.
import Foundation
import CoreText
import CoreGraphics

var fonts: [String: CTFont] = [:]
var sent = Set<String>()

func f(_ v: CGFloat) -> String {
    return String(Int(Double(v).rounded()))   // whole units of a 1000-unit em: far finer than a screen pixel
}

func outline(_ font: CTFont, _ glyph: CGGlyph) -> String {
    guard let path = CTFontCreatePathForGlyph(font, glyph, nil) else { return "" }
    var d = ""
    path.applyWithBlock { e in
        let p = e.pointee.points
        switch e.pointee.type {
        case .moveToPoint: d += "M\(f(p[0].x)) \(f(-p[0].y))"
        case .addLineToPoint: d += "L\(f(p[0].x)) \(f(-p[0].y))"
        case .addQuadCurveToPoint: d += "Q\(f(p[0].x)) \(f(-p[0].y)) \(f(p[1].x)) \(f(-p[1].y))"
        case .addCurveToPoint: d += "C\(f(p[0].x)) \(f(-p[0].y)) \(f(p[1].x)) \(f(-p[1].y)) \(f(p[2].x)) \(f(-p[2].y))"
        case .closeSubpath: d += "Z"
        @unknown default: break
        }
    }
    return d
}

while let line = readLine() {
    guard let data = line.data(using: .utf8),
          let req = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
          let name = req["font"] as? String, let text = req["text"] as? String else {
        print("{\"error\":\"bad request\"}"); continue
    }
    if fonts[name] == nil {
        let font = CTFontCreateWithName(name as CFString, 1000, nil)
        let got = CTFontCopyPostScriptName(font) as String
        if got != name { FileHandle.standardError.write("textpath: asked for \(name), system gave \(got)\n".data(using: .utf8)!) }
        fonts[name] = font
    }
    let font = fonts[name]!
    let attrs: [NSAttributedString.Key: Any] = [
        NSAttributedString.Key(kCTFontAttributeName as String): font,
        NSAttributedString.Key(kCTLigatureAttributeName as String): 0,
    ]
    let ctline = CTLineCreateWithAttributedString(NSAttributedString(string: text, attributes: attrs))
    var glyphs: [[Any]] = []
    var fresh: [String: String] = [:]
    for run in CTLineGetGlyphRuns(ctline) as! [CTRun] {
        let n = CTRunGetGlyphCount(run)
        var gs = [CGGlyph](repeating: 0, count: n)
        var ps = [CGPoint](repeating: .zero, count: n)
        CTRunGetGlyphs(run, CFRange(location: 0, length: n), &gs)
        CTRunGetPositions(run, CFRange(location: 0, length: n), &ps)
        let runFont = (CTRunGetAttributes(run) as NSDictionary)[kCTFontAttributeName as String] as! CTFont
        let runName = CTFontCopyPostScriptName(runFont) as String
        for i in 0..<n {
            let key = "\(runName):\(gs[i])"
            glyphs.append([key, (Double(ps[i].x) * 10).rounded() / 10])
            if !sent.contains(key) { sent.insert(key); fresh[key] = outline(runFont, gs[i]) }
        }
    }
    let width = CTLineGetTypographicBounds(ctline, nil, nil, nil)
    let out: [String: Any] = [
        "glyphs": glyphs, "width": (width * 10).rounded() / 10, "new": fresh,
        "ascent": Double(CTFontGetAscent(font)), "descent": Double(CTFontGetDescent(font)),
        "cap": Double(CTFontGetCapHeight(font)), "x": Double(CTFontGetXHeight(font)),
    ]
    let json = try! JSONSerialization.data(withJSONObject: out)
    print(String(data: json, encoding: .utf8)!)
    fflush(stdout)
}
