#include "ShaderCompilerInternal.h"

#include <Comparison.hpp>
#include <SystemError.hpp>
#include <TextPrimitives.hpp>
#include <Types.h>
#include <Utility.hpp>

namespace rox::shader
{
	using namespace core;

	namespace
	{
		StringView trimIndent(StringView text)
		{
			while (!text.IsEmpty() && (text[0] == ' ' || text[0] == '\t'))
			{
				text = text.Substr(1);
			}

			return text;
		}

		StringView readDiagnosticLine(StringView messages, usize begin, usize& end)
		{
			end = messages.Find('\n', begin);
			if (end == StringView::NPOS)
			{
				end = messages.Size();
			}

			StringView line = messages.Substr(begin, end - begin);
			if (line.EndsWith("\r"))
			{
				line = line.Substr(0, line.Size() - 1);
			}

			return line;
		}

		bool parseArrowLocation(StringView text, StringView& identity, uint32& line, uint32& column)
		{
			text = trimIndent(text);
			if (!text.StartsWith("--> "))
			{
				return false;
			}

			text = text.Substr(4);
			const usize colon = text.Find(':');
			if (colon == StringView::NPOS || !IsCanonicalIdentity(text.Substr(0, colon)))
			{
				return false;
			}

			const StringView rest = text.Substr(colon + 1);
			const FromCharsResult parsedLine = FromChars(rest.Data(), rest.Data() + rest.Size(), line);
			if (parsedLine.ec != EErrorCondition() || line == 0 || parsedLine.ptr == rest.Data() + rest.Size() || *parsedLine.ptr != ':')
			{
				return false;
			}

			const FromCharsResult parsedColumn = FromChars(parsedLine.ptr + 1, rest.Data() + rest.Size(), column);
			if (parsedColumn.ec != EErrorCondition() || column == 0 || parsedColumn.ptr != rest.Data() + rest.Size())
			{
				return false;
			}

			identity = text.Substr(0, colon);

			return true;
		}

		bool isPrettyContext(StringView line)
		{
			line = trimIndent(line);
			if (line.StartsWith("|") || line == "--'")
			{
				return true;
			}

			usize digits = 0;

			while (digits < line.Size() && line[digits] >= '0' && line[digits] <= '9')
			{
				++digits;
			}

			return digits != 0 && trimIndent(line.Substr(digits)).StartsWith("|");
		}
	} // namespace

	void AddDiagnostics(ShaderBuildReport& report, StringView messages, const ShaderCompileRequest& request, uint32 entry, uint32 target)
	{
		usize total = 0;

		for (const ShaderDiagnostic& diagnostic : report.Diagnostics)
		{
			total += diagnostic.Message.Size();
		}

		usize begin = 0;

		while (begin < messages.Size())
		{
			const usize messageStart = begin;
			usize end = 0;
			const StringView line = readDiagnosticLine(messages, begin, end);
			begin = end + 1;
			if (line.IsEmpty())
			{
				continue;
			}

			StringView message = line;
			StringView arrowIdentity;
			uint32 arrowLine = 0;
			uint32 arrowColumn = 0;
			bool bArrowLocation = false;
			if (begin < messages.Size() && (line.StartsWith("error[") || line.StartsWith("warning[") || line.StartsWith("note[") || line.StartsWith("fatal error[")))
			{
				usize arrowEnd = 0;
				const StringView arrow = readDiagnosticLine(messages, begin, arrowEnd);
				bArrowLocation = parseArrowLocation(arrow, arrowIdentity, arrowLine, arrowColumn);
				if (bArrowLocation)
				{
					// Pretty diagnostics carry the useful source excerpt after their location line.
					end = arrowEnd;
					begin = arrowEnd + 1;

					while (begin < messages.Size())
					{
						usize contextEnd = 0;
						const StringView context = readDiagnosticLine(messages, begin, contextEnd);
						if (!isPrettyContext(context))
						{
							break;
						}

						end = contextEnd;
						begin = contextEnd + 1;
						if (trimIndent(context) == "--'")
						{
							break;
						}
					}

					message = messages.Substr(messageStart, end - messageStart);
				}
			}

			if (report.Diagnostics.Size() >= request.Limits.MaximumDiagnostics ||
				message.Size() > request.Limits.MaximumDiagnosticBytes - Min<usize>(total, request.Limits.MaximumDiagnosticBytes))
			{
				report.bTruncated = true;
				return;
			}

			ShaderDiagnostic diagnostic(report.Diagnostics.GetResource());
			diagnostic.EntryPointIndex = entry;
			diagnostic.TargetIndex = target;
			diagnostic.Severity = line.Contains("error") ? EShaderDiagnosticSeverity::Error
				: line.Contains("warning")				 ? EShaderDiagnosticSeverity::Warning
														: EShaderDiagnosticSeverity::Note;
			if (!diagnostic.DiagnosticID.Assign("ROX-SHADER-COMPILER") ||
				!diagnostic.Message.Assign(IsValidUtf8(message) ? message : StringView("Compiler diagnostic contains malformed UTF-8.")))
			{
				report.bAllocationFailure = true;
				report.bTruncated = true;
				return;
			}

			// Slang emits error[E12345]; DXC may emit error X1234: or warning X1234:.
			usize codeStart = StringView::NPOS;
			usize codeLength = 0;

			for (StringView const prefix : {StringView("error["), StringView("warning["), StringView("note[")})
			{
				const usize match = line.Find(prefix);
				if (match == StringView::NPOS)
				{
					continue;
				}

				const usize start = match + prefix.Size();
				const usize close = line.Find(']', start);
				if (close != StringView::NPOS && close > start && close - start <= 64)
				{
					codeStart = start;
					codeLength = close - start;
				}

				break;
			}

			if (codeStart == StringView::NPOS)
			{
				for (StringView const prefix : {StringView("error "), StringView("warning ")})
				{
					const usize match = line.Find(prefix);
					if (match == StringView::NPOS)
					{
						continue;
					}

					const usize start = match + prefix.Size();
					usize codeEnd = start;

					while (codeEnd < line.Size() && ((line[codeEnd] >= 'A' && line[codeEnd] <= 'Z') || (line[codeEnd] >= '0' && line[codeEnd] <= '9')))
					{
						++codeEnd;
					}

					if (codeEnd > start && codeEnd - start <= 64 && codeEnd < line.Size() && line[codeEnd] == ':')
					{
						codeStart = start;
						codeLength = codeEnd - start;
					}

					break;
				}
			}

			if (codeStart != StringView::NPOS && !diagnostic.CompilerCode.Assign(line.Substr(codeStart, codeLength)))
			{
				report.bAllocationFailure = true;
				report.bTruncated = true;
				return;
			}

			// Parse only a proven source prefix. Unrecognized diagnostic lines retain an unknown location.
			const usize colon = line.Find(':');
			const usize open = line.Find('(');
			const uint64 locationStart = Min(colon, open);
			if (locationStart != StringView::NPOS && IsCanonicalIdentity(line.Substr(0, locationStart)))
			{
				const StringView rest = line.Substr(locationStart + 1);
				uint32 number = 0;
				FromCharsResult parsed = FromChars(rest.Data(), rest.Data() + rest.Size(), number);
				if (parsed.ec == EErrorCondition() && number != 0)
				{
					diagnostic.Line = number;
					if (!diagnostic.SourceIdentity.Assign(line.Substr(0, locationStart)))
					{
						report.bAllocationFailure = true;
						report.bTruncated = true;
						return;
					}

					if (parsed.ptr < rest.Data() + rest.Size() && (*parsed.ptr == ',' || *parsed.ptr == ':'))
					{
						FromCharsResult column = FromChars(parsed.ptr + 1, rest.Data() + rest.Size(), number);
						if (column.ec == EErrorCondition())
						{
							diagnostic.Column = number;
						}
					}
				}
			}

			if (bArrowLocation)
			{
				if (!diagnostic.SourceIdentity.Assign(arrowIdentity))
				{
					report.bAllocationFailure = true;
					report.bTruncated = true;
					return;
				}

				diagnostic.Line = arrowLine;
				diagnostic.Column = arrowColumn;
			}

			total += message.Size();
			if (!report.Diagnostics.Add(Move(diagnostic)))
			{
				report.bAllocationFailure = true;
				report.bTruncated = true;
				return;
			}
		}
	}
} // namespace rox::shader
