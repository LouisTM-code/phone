// Renders Cyrillic glyphs into an 8x16 1-bit font (same layout as fpmenu/font8x16.h)
import CoreGraphics
import CoreText
import Foundation

let args = CommandLine.arguments
let fontName = args.count > 1 ? args[1] : "Menlo-Bold"
let size = CGFloat(args.count > 2 ? Double(args[2])! : 13.0)
let threshold = args.count > 3 ? Int(args[3])! : 110
let preview = args.count > 4

var chars: [UInt32] = Array(0x410...0x44F)
chars.append(0x401); chars.append(0x451)

let font = CTFontCreateWithName(fontName as CFString, size, nil)
let W = 8, H = 16
let ascent = CTFontGetAscent(font), descent = CTFontGetDescent(font)
let baseline = (CGFloat(H) - (ascent + descent)) / 2 + descent

var out = ""
for cp in chars {
	let ctx = CGContext(data: nil, width: W, height: H, bitsPerComponent: 8, bytesPerRow: W,
		space: CGColorSpaceCreateDeviceGray(), bitmapInfo: CGImageAlphaInfo.none.rawValue)!
	ctx.setFillColor(gray: 0, alpha: 1)
	ctx.fill(CGRect(x: 0, y: 0, width: W, height: H))
	ctx.setShouldAntialias(threshold > 0)
	let s = String(UnicodeScalar(cp)!)
	let attr = NSAttributedString(string: s, attributes: [
		NSAttributedString.Key(kCTFontAttributeName as String): font,
		NSAttributedString.Key(kCTForegroundColorFromContextAttributeName as String): true])
	ctx.setFillColor(gray: 1, alpha: 1)
	let line = CTLineCreateWithAttributedString(attr)
	let bounds = CTLineGetBoundsWithOptions(line, .useGlyphPathBounds)
	let x = (CGFloat(W) - bounds.width) / 2 - bounds.minX
	ctx.textPosition = CGPoint(x: x.rounded(), y: baseline.rounded())
	CTLineDraw(line, ctx)
	let data = ctx.data!.assumingMemoryBound(to: UInt8.self)
	var bytes: [String] = []
	for row in 0..<H {
		var b = 0
		var art = ""
		for col in 0..<W {
			let v = Int(data[row * W + col])
			if v >= (threshold > 0 ? threshold : 128) { b |= 0x80 >> col; art += "#" } else { art += "." }
		}
		bytes.append(String(format: "0x%02x", b))
		if preview { print(art) }
	}
	if preview { print("--- U+\(String(cp, radix: 16)) \(s)") }
	out += bytes.joined(separator: ",") + ", /* U+\(String(cp, radix: 16)) */\n"
}
if !preview { print(out, terminator: "") }
