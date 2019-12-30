#include "external_copy_handle.h"
#include "external_copy/external_copy.h"

using namespace v8;
using std::shared_ptr;
using std::unique_ptr;

namespace ivm {

/**
 * Transferable wrapper
 */
ExternalCopyHandle::ExternalCopyTransferable::ExternalCopyTransferable(std::shared_ptr<ExternalCopy> value) : value(std::move(value)) {}

Local<Value> ExternalCopyHandle::ExternalCopyTransferable::TransferIn() {
	return ClassHandle::NewInstance<ExternalCopyHandle>(value);
}

/**
 * ExternalCopyHandle implementation
 */
ExternalCopyHandle::ExternalCopyHandle(shared_ptr<ExternalCopy> value) : value(std::move(value)), size{this->value->Size()} {
	Isolate::GetCurrent()->AdjustAmountOfExternalAllocatedMemory(size);
}

ExternalCopyHandle::~ExternalCopyHandle() {
	Isolate::GetCurrent()->AdjustAmountOfExternalAllocatedMemory(-size);
}

Local<FunctionTemplate> ExternalCopyHandle::Definition() {
	return Inherit<TransferableHandle>(MakeClass(
		"ExternalCopy", ConstructorFunction<decltype(&New), &New>{},
		"totalExternalSize", StaticAccessor<decltype(&ExternalCopyHandle::TotalExternalSizeGetter), &ExternalCopyHandle::TotalExternalSizeGetter>{},
		"copy", MemberFunction<decltype(&ExternalCopyHandle::Copy), &ExternalCopyHandle::Copy>{},
		"copyInto", MemberFunction<decltype(&ExternalCopyHandle::CopyInto), &ExternalCopyHandle::CopyInto>{},
		"release", MemberFunction<decltype(&ExternalCopyHandle::Release), &ExternalCopyHandle::Release>{}
	));
}

unique_ptr<Transferable> ExternalCopyHandle::TransferOut() {
	return std::make_unique<ExternalCopyTransferable>(value);
}

unique_ptr<ExternalCopyHandle> ExternalCopyHandle::New(Local<Value> value, MaybeLocal<Object> maybe_options) {
	Local<Object> options;
	bool transfer_out = false;
	ArrayRange transfer_list;
	if (maybe_options.ToLocal(&options)) {
		transfer_out = ReadOption<bool>(options, "transferOut", false);
		transfer_list = ReadOption<ArrayRange>(options, "transferList", {});
	}
	return std::make_unique<ExternalCopyHandle>(shared_ptr<ExternalCopy>(ExternalCopy::Copy(value, transfer_out, transfer_list)));
}

void ExternalCopyHandle::CheckDisposed() {
	if (!value) {
		throw RuntimeGenericError("Copy has been released");
	}
}

/**
 * JS API functions
 */
Local<Value> ExternalCopyHandle::TotalExternalSizeGetter() {
	return Number::New(Isolate::GetCurrent(), ExternalCopy::TotalExternalSize());
}

Local<Value> ExternalCopyHandle::Copy(MaybeLocal<Object> maybe_options) {
	CheckDisposed();
	bool release = ReadOption<bool>(maybe_options, "release", false);
	bool transfer_in = ReadOption<bool>(maybe_options, "transferIn", false);
	Local<Value> ret = value->CopyIntoCheckHeap(transfer_in);
	if (release) {
		Release();
	}
	return ret;
}

Local<Value> ExternalCopyHandle::CopyInto(MaybeLocal<Object> maybe_options) {
	CheckDisposed();
	bool release = ReadOption<bool>(maybe_options, "release", false);
	bool transfer_in = ReadOption<bool>(maybe_options, "transferIn", false);
	Local<Value> ret = ClassHandle::NewInstance<ExternalCopyIntoHandle>(value, transfer_in);
	if (release) {
		Release();
	}
	return ret;
}

Local<Value> ExternalCopyHandle::Release() {
	CheckDisposed();
	Isolate::GetCurrent()->AdjustAmountOfExternalAllocatedMemory(-std::exchange(size, 0));
	value.reset();
	return Undefined(Isolate::GetCurrent());
}

/**
 * ExternalCopyIntoHandle implementation
 */
ExternalCopyIntoHandle::ExternalCopyIntoTransferable::ExternalCopyIntoTransferable(shared_ptr<ExternalCopy> value, bool transfer_in) : value(std::move(value)), transfer_in(transfer_in) {}

Local<Value> ExternalCopyIntoHandle::ExternalCopyIntoTransferable::TransferIn() {
	return value->CopyIntoCheckHeap(transfer_in);
}

ExternalCopyIntoHandle::ExternalCopyIntoHandle(shared_ptr<ExternalCopy> value, bool transfer_in) : value(std::move(value)), transfer_in(transfer_in) {}

Local<FunctionTemplate> ExternalCopyIntoHandle::Definition() {
	return Inherit<TransferableHandle>(MakeClass("ExternalCopyInto", nullptr));
}

unique_ptr<Transferable> ExternalCopyIntoHandle::TransferOut() {
	if (!value) {
		throw RuntimeGenericError("The return value of `copyInto()` should only be used once");
	}
	return std::make_unique<ExternalCopyIntoTransferable>(std::move(value), transfer_in);
}

} // namespace ivm
