#include <TraceCapture.h>
#include <Algorithms.hpp>
#include <Types.h>
#include <NumericLimits.hpp>
#include <Utility.hpp>
#include <algorithm>
#include <limits>

namespace rox::core
{
	bool AnalyzeTraceSnapshot(const TraceSnapshot& snapshot, Array<TraceScopeStatistics>& output) noexcept
	{
		struct ScopeIndex final
		{
			rox::core::uint64 Id = 0;
			rox::core::usize Row = 0;
		};

		Array<ScopeIndex> indices(output.GetResource());
		Array<TraceScopeStatistics> candidate(output.GetResource());

		if (!indices.Reserve(snapshot.Events.Size()) || !candidate.Reserve(snapshot.Events.Size()))
		{
			return false;
		}

		for (rox::core::usize i = 0; i < snapshot.Events.Size(); ++i)
		{
			const auto& event = snapshot.Events[i];

			if (event.Type != ETraceEventType::Scope)
			{
				continue;
			}

			if (event.ScopeId == 0 || event.ThreadId == 0 || event.ParentScopeId >= event.ScopeId ||
			    event.CaptureId == 0 || event.CaptureId != snapshot.CaptureId ||
			    event.DurationNanoseconds > rox::core::NumericLimits<rox::core::uint64>::max() - event.TimestampNanoseconds)
			{
				return false;
			}

			indices.AddRequired({event.ScopeId, candidate.Size()});
			candidate.AddRequired({i, event.DurationNanoseconds, event.DurationNanoseconds, false});
		}

		if (!indices.IsEmpty())
		{
			rox::core::Sort(indices.begin(), indices.end(), [](const ScopeIndex& left, const ScopeIndex& right) { return left.Id < right.Id; });
		}

		for (rox::core::usize i = 1; i < indices.Size(); ++i)
		{
			if (indices[i - 1].Id == indices[i].Id)
			{
				return false;
			}
		}

		Array<rox::core::uint64> childEnds(output.GetResource());
		if (!childEnds.Reserve(candidate.Size()))
		{
			return false;
		}

		for (rox::core::usize i = 0; i < candidate.Size(); ++i)
		{
			childEnds.AddRequired(0);
		}

		for (const auto& index : indices)
		{
			auto& row = candidate[index.Row];
			const auto& event = snapshot.Events[row.EventIndex];

			if (event.ParentScopeId == 0)
			{
				continue;
			}

			const auto found = rox::core::LowerBound(indices.begin(), indices.end(), event.ParentScopeId,
			    [](const ScopeIndex& entry, rox::core::uint64 id) { return entry.Id < id; });
			if (found == indices.end() || found->Id != event.ParentScopeId)
			{
				row.bIncomplete = true;
				continue;
			}

			auto& parentRow = candidate[found->Row];
			const auto& parent = snapshot.Events[parentRow.EventIndex];
			if (event.ParentScopeId >= event.ScopeId || event.ThreadId != parent.ThreadId || event.CaptureId != parent.CaptureId ||
			    event.TimestampNanoseconds < parent.TimestampNanoseconds ||
			    event.TimestampNanoseconds + event.DurationNanoseconds > parent.TimestampNanoseconds + parent.DurationNanoseconds ||
			    event.DurationNanoseconds > parentRow.ExclusiveNanoseconds)
			{
				return false;
			}

			if (event.TimestampNanoseconds < childEnds[found->Row])
			{
				return false;
			}

			childEnds[found->Row] = event.TimestampNanoseconds + event.DurationNanoseconds;

			if (parentRow.Depth == rox::core::NumericLimits<rox::core::uint32>::max())
			{
				return false;
			}

			row.Depth = parentRow.Depth + 1;
			row.bIncomplete = row.bIncomplete || parentRow.bIncomplete;
			parentRow.ExclusiveNanoseconds -= event.DurationNanoseconds;
		}

		output = rox::core::Move(candidate);
		return true;
	}
}
