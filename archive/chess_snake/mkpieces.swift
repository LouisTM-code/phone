// Renders chess piece glyphs into 1-bit masks: silhouette, edge and inner detail lines
import CoreGraphics
import CoreText
import Foundation

let N = Int(CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "26")!
let fontName = CommandLine.arguments.count > 2 ? CommandLine.arguments[2] : "Apple Symbols"
let preview = CommandLine.arguments.count > 3
// pawn, knight, bishop, rook, queen, king (outline glyphs)
let codes: [UInt32] = [0x2659, 0x2658, 0x2657, 0x2656, 0x2655, 0x2654]

func render(_ cp: UInt32) -> [[Bool]] {
	let probe = CTFontCreateWithName(fontName as CFString, 100, nil)
	var ch = UniChar(cp), glyph = CGGlyph(0)
	CTFontGetGlyphsForCharacters(probe, &ch, &glyph, 1)
	let path = CTFontCreatePathForGlyph(probe, glyph, nil)!
	let bb = path.boundingBox
	let scale = CGFloat(N - 1) / max(bb.width, bb.height)
	let ctx = CGContext(data: nil, width: N, height: N, bitsPerComponent: 8, bytesPerRow: N,
		space: CGColorSpaceCreateDeviceGray(), bitmapInfo: CGImageAlphaInfo.none.rawValue)!
	ctx.setFillColor(gray: 0, alpha: 1)
	ctx.fill(CGRect(x: 0, y: 0, width: N, height: N))
	ctx.translateBy(x: (CGFloat(N) - bb.width * scale) / 2 - bb.minX * scale,
		y: 0.5 - bb.minY * scale)
	ctx.scaleBy(x: scale, y: scale)
	ctx.addPath(path)
	ctx.setFillColor(gray: 1, alpha: 1)
	ctx.fillPath()
	let d = ctx.data!.assumingMemoryBound(to: UInt8.self)
	var m = Array(repeating: Array(repeating: false, count: N), count: N)
	for y in 0..<N { for x in 0..<N { m[y][x] = d[y * N + x] >= 110 } }
	return m
}

var out = "/* generated chess piece masks, \(N)x\(N), bit (N-1-x) = column x */\n"
var sil = "static const uint32_t piece_sil[7][\(N)] = {\n\t{ 0 },\n"
var edge = "static const uint32_t piece_edge[7][\(N)] = {\n\t{ 0 },\n"
var det = "static const uint32_t piece_detail[7][\(N)] = {\n\t{ 0 },\n"
for cp in codes {
	let o = render(cp)
	// outside = flood fill from the border through non-outline pixels
	var outside = Array(repeating: Array(repeating: false, count: N), count: N)
	var stack: [(Int, Int)] = []
	for i in 0..<N { stack += [(i, 0), (i, N - 1), (0, i), (N - 1, i)] }
	while let (x, y) = stack.popLast() {
		if x < 0 || y < 0 || x >= N || y >= N || outside[y][x] || o[y][x] { continue }
		outside[y][x] = true
		stack += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
	}
	var rs: [String] = [], re: [String] = [], rd: [String] = []
	for y in 0..<N {
		var s = 0, e = 0, dd = 0
		var art = ""
		for x in 0..<N {
			let bit = 1 << (N - 1 - x)
			if !outside[y][x] { s |= bit }
			if o[y][x] {
				var nearOut = false
				for (dx, dy) in [(1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1), (1, -1), (-1, 1)] {
					let nx = x + dx, ny = y + dy
					if nx < 0 || ny < 0 || nx >= N || ny >= N || outside[ny][nx] { nearOut = true }
				}
				if nearOut { e |= bit; art += "#" } else { dd |= bit; art += "+" }
			} else { art += outside[y][x] ? "." : " " }
		}
		if preview { print(art) }
		rs.append(String(format: "0x%07x", s)); re.append(String(format: "0x%07x", e)); rd.append(String(format: "0x%07x", dd))
	}
	if preview { print("---") }
	sil += "\t{ " + rs.joined(separator: ",") + " },\n"
	edge += "\t{ " + re.joined(separator: ",") + " },\n"
	det += "\t{ " + rd.joined(separator: ",") + " },\n"
}
out += sil + "};\n" + edge + "};\n" + det + "};\n"
if !preview { print(out, terminator: "") }
