/*
 * The MIT License
 *
 * Copyright (c) 2010 Sam Day
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "index.h"
#include "index_entry.h"
#include "repository.h"

#define LENGTH_PROPERTY String::NewSymbol("entryCount")

namespace gitteh {

struct index_data {
	int entryCount;
};

struct entry_request {
	Persistent<Function> callback;
	Index *indexObj;
	int index;
	git_index_entry *entry;
	int error;
};

Persistent<FunctionTemplate> Index::constructor_template;

Index::Index() {
	entryFactory_ = new ObjectFactory<Index, IndexEntry, git_index_entry>(this);
}

Index::~Index() {
	repository_->notifyIndexDead();
	git_index_free(index_);
}

void Index::Init(Handle<Object> target) {
	HandleScope scope;

	Local<FunctionTemplate> t = FunctionTemplate::New(New);
	constructor_template = Persistent<FunctionTemplate>::New(t);
	constructor_template->SetClassName(String::New("Index"));
	t->InstanceTemplate()->SetInternalFieldCount(1);

	NODE_SET_PROTOTYPE_METHOD(t, "getEntry", GetEntry);
}

Handle<Value> Index::New(const Arguments& args) {
	HandleScope scope;

	Index *index = new Index();
	index->Wrap(args.This());

	return args.This();
}

Handle<Value> Index::GetEntry(const Arguments& args) {
	HandleScope scope;
	Index *index = ObjectWrap::Unwrap<Index>(args.This());

	REQ_ARGS(1)
	REQ_INT_ARG(0, indexArg);

	if(HAS_CALLBACK_ARG) {
		entry_request *request = new entry_request;
		REQ_FUN_ARG(args.Length() - 1, callbackArg);

		request->indexObj = index;
		request->callback = Persistent<Function>::New(callbackArg);
		request->index = indexArg;
		request->entry = NULL;

		index->Ref();
		eio_custom(EIO_GetEntry, EIO_PRI_DEFAULT, EIO_AfterGetEntry, request);
		ev_ref(EV_DEFAULT_UC);

		return Undefined();
	}
	else {
		index->repository_->lockRepository();
		git_index_entry *entry = git_index_get(index->index_, indexArg);
		index->repository_->unlockRepository();

		if(entry == NULL) {
			THROW_ERROR("Invalid entry.");
		}

		IndexEntry *entryObject = index->entryFactory_->syncRequestObject(entry);
		return scope.Close(entryObject->handle_);
	}
}

int Index::EIO_GetEntry(eio_req *req) {
	entry_request *reqData = static_cast<entry_request*>(req->data);

	reqData->indexObj->repository_->lockRepository();
	reqData->entry = git_index_get(reqData->indexObj->index_, reqData->index);
	reqData->indexObj->repository_->unlockRepository();

	return 0;
}

int Index::EIO_AfterGetEntry(eio_req *req) {
	HandleScope scope;
	entry_request *reqData = static_cast<entry_request*>(req->data);

	ev_unref(EV_DEFAULT_UC);
 	reqData->indexObj->Unref();

	Handle<Value> callbackArgs[2];
 	if(reqData->entry == NULL) {
 		Handle<Value> error = Exception::Error(String::New("Couldn't get index entry."));
 		callbackArgs[0] = error;
 		callbackArgs[1] = Null();

 		TRIGGER_CALLBACK();

 		reqData->callback.Dispose();
	}
	else {
		reqData->indexObj->entryFactory_->asyncRequestObject(
				reqData->entry, reqData->callback);
	}

	delete reqData;

	return 0;
}

void Index::processInitData(void *data) {
	if(data != NULL) {
		index_data *indexData = static_cast<index_data*>(data);

		entryCount_ = indexData->entryCount;
		handle_->Set(LENGTH_PROPERTY, Integer::New(indexData->entryCount),
				(PropertyAttribute)(ReadOnly | DontDelete));

		delete indexData;
	}
}

void *Index::loadInitData() {
	repository_->lockRepository();
	initError_ = git_repository_index(&index_, repository_->repo_);
	repository_->unlockRepository();
	if(initError_ == GIT_EBAREINDEX) {
		repository_->lockRepository();
		initError_ = git_index_open_bare(&index_, repository_->path_);
		repository_->unlockRepository();
	}

	if(initError_ != GIT_SUCCESS) {
		return NULL;
	}

	index_data *data = new index_data;

	repository_->lockRepository();
	git_index_read(index_);
	data->entryCount = git_index_entrycount(index_);
	repository_->unlockRepository();

	return data;
}

void Index::setOwner(void *owner) {
	repository_ = static_cast<Repository*>(owner);
}

} // namespace gitteh
