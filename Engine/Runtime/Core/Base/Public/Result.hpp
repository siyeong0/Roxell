#pragma once

#include <Assertion.h>
#include <Log.h>
#include <Vocabulary.hpp>
#include <Types.h>
#include <TypeTraits.hpp>
#include <Utility.hpp>
#include <CoreString.hpp>
#include <StringView.hpp>

#include <type_traits>
#include <utility>
#include <variant>

namespace rox::core
{
	enum class EErrorCode : uint32
	{
		None,
		InvalidArgument,
		NotFound,
		Conflict,
		PermissionDenied,
		Unsupported,
		Cancelled,
		ExternalFailure,
		InternalFailure,
		Timeout
	};

	namespace detail
	{
		inline const char* ErrorCodeName(EErrorCode code) noexcept
		{
			switch (code)
			{
			case EErrorCode::None:
				return "None";
			case EErrorCode::InvalidArgument:
				return "InvalidArgument";
			case EErrorCode::NotFound:
				return "NotFound";
			case EErrorCode::Conflict:
				return "Conflict";
			case EErrorCode::PermissionDenied:
				return "PermissionDenied";
			case EErrorCode::Unsupported:
				return "Unsupported";
			case EErrorCode::Cancelled:
				return "Cancelled";
			case EErrorCode::ExternalFailure:
				return "ExternalFailure";
			case EErrorCode::InternalFailure:
				return "InternalFailure";
			case EErrorCode::Timeout:
				return "Timeout";
			default:
				return "Unknown";
			}
		}

		inline void LogResultFailure(
		    EErrorCode code, StringView diagnosticID, StringView message, const char* stage, SourceLocation source) noexcept
		{
			if (!IsResultFailureTracingEnabled())
			{
				return;
			}
			char expression[64] = {};
			FormatText(expression, sizeof(expression), "%s:%s", stage, ErrorCodeName(code));
			WriteLog({ELogLevel::Error,
			          diagnosticID.IsEmpty() ? DiagnosticText("Result") : DiagnosticText(diagnosticID),
			          message.IsEmpty() ? DiagnosticText("Operation failed.") : DiagnosticText(message),
			          expression,
			          source});
		}
	} // namespace detail

	// Copies preserve Code; MakeLossyString may replace text when a resource is exhausted.
	// Copy construction uses the source resources, while assignment keeps the destination resources.
	// Moves empty the source strings but preserve its Code and resource identities. Self-assignment is a no-op.
	// Each resource must outlive its field; concurrent mutation is unsupported.
	struct Error
	{
		EErrorCode Code = EErrorCode::None;

		String DiagnosticID;
		String Message;
		String RecoveryHint;

		Error() noexcept = default;

		Error(EErrorCode code, String&& diagnosticID, String&& message, String&& recoveryHint) noexcept
		    : Code(code)
		    , DiagnosticID(rox::core::Move(diagnosticID))
		    , Message(rox::core::Move(message))
		    , RecoveryHint(rox::core::Move(recoveryHint))
		{
		}

		Error(const Error& other) noexcept
		    : Code(other.Code)
		    , DiagnosticID(MakeLossyString(other.DiagnosticID.View(), other.DiagnosticID.GetResource()))
		    , Message(MakeLossyString(other.Message.View(), other.Message.GetResource()))
		    , RecoveryHint(MakeLossyString(other.RecoveryHint.View(), other.RecoveryHint.GetResource()))
		{
		}

		Error(Error&& other) noexcept = default;

		Error& operator=(const Error& other) noexcept
		{
			if (this == &other)
			{
				return *this;
			}

			Code = other.Code;
			DiagnosticID = MakeLossyString(other.DiagnosticID.View(), DiagnosticID.GetResource());
			Message = MakeLossyString(other.Message.View(), Message.GetResource());
			RecoveryHint = MakeLossyString(other.RecoveryHint.View(), RecoveryHint.GetResource());

			return *this;
		}

		Error& operator=(Error&& other) noexcept
		{
			if (this == &other)
			{
				return *this;
			}

			Code = other.Code;
			moveText(DiagnosticID, other.DiagnosticID);
			moveText(Message, other.Message);
			moveText(RecoveryHint, other.RecoveryHint);

			return *this;
		}

	private:
		static void moveText(String& destination, String& source) noexcept
		{
			if (&destination.GetResource() == &source.GetResource())
			{
				destination = rox::core::Move(source);
			}
			else
			{
				destination = MakeLossyString(source.View(), destination.GetResource());
				source.Reset();
			}
		}
	};

	// Use for borrowed text, including untrusted input. MakeLossyString prevents malformed UTF-8 or resource exhaustion from turning a recoverable failure into an abort.
	// With Result failure tracing enabled, creation logs and may synchronously call the diagnostics provider.
	// Failure(Error) also logs propagation; see SetResultFailureTracingEnabled.
	[[nodiscard]] inline Error MakeError(
	    const EErrorCode code, const StringView diagnosticID, const StringView message,
	    const StringView recoveryHint = {}, const SourceLocation source = SourceLocation::current()) noexcept
	{
		ASSERT(code != EErrorCode::None, "An error must contain a non-success code.");
		detail::LogResultFailure(code, diagnosticID, message, "MakeError", source);
		return Error{code, MakeLossyString(diagnosticID), MakeLossyString(message), MakeLossyString(recoveryHint)};
	}

	template <typename TValue> class [[nodiscard]] Result
	{
		static_assert(!rox::core::IsReference<TValue>);

	public:
		// References support over-aligned values without a by-value calling convention.
		[[nodiscard]] static Result Success(TValue&& value)
		{
			return Result(rox::core::InPlaceIndex<0>, rox::core::Move(value));
		}

		[[nodiscard]] static Result Success(const TValue& value)
		    requires rox::core::IsCopyConstructible<TValue>
		{
			TValue copy(value);
			return Success(rox::core::Move(copy));
		}

		// Copy directly into the result; any copy exception propagates to the caller.
		[[nodiscard]] static Result SuccessCopy(const TValue& value)
		    requires rox::core::IsCopyConstructible<TValue>
		{
			return Result(rox::core::InPlaceIndex<0>, value);
		}

		[[nodiscard]] static Result Failure(
		    EErrorCode code, StringView diagnosticID, StringView message,
		    StringView recoveryHint = {}, SourceLocation source = SourceLocation::current())
		{
			return Result(rox::core::InPlaceIndex<1>, MakeError(code, diagnosticID, message, recoveryHint, source));
		}

		[[nodiscard]] static Result Failure(Error error, SourceLocation source = SourceLocation::current())
		{
			ASSERT(error.Code != EErrorCode::None, "A failed Result must contain a non-success error code.");
			detail::LogResultFailure(error.Code, error.DiagnosticID.View(), error.Message.View(), "Failure", source);
			return Result(rox::core::InPlaceIndex<1>, rox::core::Move(error));
		}

		[[nodiscard]] bool HasValue() const noexcept { return m_Storage.index() == 0; }
		[[nodiscard]] explicit operator bool() const noexcept { return HasValue(); }

		[[nodiscard]] TValue& Value() &
		{
			ASSERT(HasValue(), "Result does not contain a value.");
			return rox::core::Get<0>(m_Storage);
		}

		[[nodiscard]] const TValue& Value() const&
		{
			ASSERT(HasValue(), "Result does not contain a value.");
			return rox::core::Get<0>(m_Storage);
		}

		[[nodiscard]] TValue&& TakeValue() &&
		{
			ASSERT(HasValue(), "Result does not contain a value.");
			return rox::core::Move(rox::core::Get<0>(m_Storage));
		}

		[[nodiscard]] Error& GetError() &
		{
			ASSERT(!HasValue(), "Result does not contain an error.");
			return rox::core::Get<1>(m_Storage);
		}

		[[nodiscard]] const Error& GetError() const&
		{
			ASSERT(!HasValue(), "Result does not contain an error.");
			return rox::core::Get<1>(m_Storage);
		}

	private:
		template <rox::core::usize Index, typename TValueArgument>
		explicit Result(rox::core::InPlaceIndexTag<Index> index, TValueArgument&& value)
		    : m_Storage(index, rox::core::Forward<TValueArgument>(value))
		{
		}

	private:
		rox::core::Variant<TValue, Error> m_Storage;
	};

	template <> class [[nodiscard]] Result<void>
	{
	public:
		[[nodiscard]] static Result Success() noexcept { return Result(); }

		[[nodiscard]] static Result Failure(
		    EErrorCode code, StringView diagnosticID, StringView message,
		    StringView recoveryHint = {}, SourceLocation source = SourceLocation::current())
		{
			return Result(MakeError(code, diagnosticID, message, recoveryHint, source));
		}

		[[nodiscard]] static Result Failure(Error error, SourceLocation source = SourceLocation::current())
		{
			ASSERT(error.Code != EErrorCode::None, "A failed Result must contain a non-success error code.");
			detail::LogResultFailure(error.Code, error.DiagnosticID.View(), error.Message.View(), "Failure", source);
			return Result(rox::core::Move(error));
		}

		[[nodiscard]] bool HasValue() const noexcept { return m_Error.Code == EErrorCode::None; }
		[[nodiscard]] explicit operator bool() const noexcept { return HasValue(); }

		void Value() const { ASSERT(HasValue(), "Result does not contain a value."); }

		[[nodiscard]] Error& GetError() &
		{
			ASSERT(!HasValue(), "Result does not contain an error.");
			return m_Error;
		}

		[[nodiscard]] const Error& GetError() const&
		{
			ASSERT(!HasValue(), "Result does not contain an error.");
			return m_Error;
		}

	private:
		// Error initializes its fields without first zeroing unused string storage.
		Result() noexcept {}

		explicit Result(Error error)
		    : m_Error(rox::core::Move(error))
		{
		}

	private:
		Error m_Error;
	};
} // namespace rox::core
