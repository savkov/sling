// Copyright 2017 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "sling/pyapi/pyparser.h"

#include "sling/nlp/document/document.h"
#include "sling/nlp/document/document-tokenizer.h"
#include "sling/nlp/document/lex.h"
#include "sling/nlp/parser/parser.h"
#include "sling/nlp/parser/trainer/frame-evaluation.h"
#include "sling/pyapi/pyframe.h"
#include "sling/pyapi/pystore.h"

namespace sling {

// Python type declarations.
PyTypeObject PyTokenizer::type;
PyMethodTable PyTokenizer::methods;
PyTypeObject PyParser::type;
PyMethodTable PyParser::methods;

void PyTokenizer::Define(PyObject *module) {
  InitType(&type, "sling.api.Tokenizer", sizeof(PyTokenizer), true);

  type.tp_init = method_cast<initproc>(&PyTokenizer::Init);
  type.tp_dealloc = method_cast<destructor>(&PyTokenizer::Dealloc);

  methods.Add("tokenize", &PyTokenizer::Tokenize);
  methods.Add("lex", &PyTokenizer::Lex);
  type.tp_methods = methods.table();

  RegisterType(&type, module, "Tokenizer");
}

int PyTokenizer::Init(PyObject *args, PyObject *kwds) {
  // Initialize tokenizer.
  tokenizer = new nlp::DocumentTokenizer();
  return 0;
}

void PyTokenizer::Dealloc() {
  delete tokenizer;
  Free();
}

PyObject *PyTokenizer::Tokenize(PyObject *args) {
  // Get arguments.
  PyStore *pystore;
  PyObject *text;
  if (!PyArg_ParseTuple(args, "OS", &pystore, &text)) return nullptr;
  if (!PyStore::TypeCheck(pystore)) return nullptr;
  if (!pystore->Writable()) return nullptr;

  // Get text.
  char *data;
  Py_ssize_t length;
  PyString_AsStringAndSize(text, &data, &length);

  // Initialize empty document.
  nlp::Document document(pystore->store);

  // Tokenize text.
  tokenizer->Tokenize(&document, Text(data, length));
  document.Update();

  // Create document frame wrapper.
  PyFrame *frame = PyObject_New(PyFrame, &PyFrame::type);
  frame->Init(pystore, document.top().handle());
  return frame->AsObject();
}

PyObject *PyTokenizer::Lex(PyObject *args) {
  // Get arguments.
  PyStore *pystore;
  PyObject *lex;
  if (!PyArg_ParseTuple(args, "OS", &pystore, &lex)) return nullptr;
  if (!PyStore::TypeCheck(pystore)) return nullptr;
  if (!pystore->Writable()) return nullptr;

  // Get text.
  char *data;
  Py_ssize_t length;
  PyString_AsStringAndSize(lex, &data, &length);

  // Initialize empty document.
  nlp::Document document(pystore->store);

  // Parse LEX-encoded text.
  nlp::DocumentLexer lexer(tokenizer);
  if (!lexer.Lex(&document, Text(data, length))) {
    PyErr_SetString(PyExc_ValueError, "Invalid LEX encoding");
    return nullptr;
  }

  // Create document frame wrapper.
  PyFrame *frame = PyObject_New(PyFrame, &PyFrame::type);
  frame->Init(pystore, document.top().handle());
  return frame->AsObject();
}


void PyParser::Define(PyObject *module) {
  InitType(&type, "sling.api.Parser", sizeof(PyParser), true);

  type.tp_init = method_cast<initproc>(&PyParser::Init);
  type.tp_dealloc = method_cast<destructor>(&PyParser::Dealloc);

  methods.Add("parse", &PyParser::Parse);
  type.tp_methods = methods.table();

  RegisterType(&type, module, "Parser");
}

int PyParser::Init(PyObject *args, PyObject *kwds) {
  // Get arguments.
  PyStore *pystore;
  char *filename;
  if (!PyArg_ParseTuple(args, "Os", &pystore, &filename)) return -1;
  if (!PyStore::TypeCheck(pystore)) return -1;
  if (!pystore->Writable()) return -1;

  // Save reference to store to keep it alive.
  this->pystore = pystore;
  Py_INCREF(pystore);

  // Load parser.
  parser = new nlp::Parser();
  parser->Load(pystore->store, filename);

  return 0;
}

void PyParser::Dealloc() {
  // Delete parser.
  delete parser;

  // Release reference to store.
  if (pystore) Py_DECREF(pystore);

  // Free object.
  Free();
}

PyObject *PyParser::Parse(PyObject *args) {
  // Get arguments.
  PyFrame *pyframe;
  if (!PyArg_ParseTuple(args, "O", &pyframe)) return nullptr;
  if (!PyFrame::TypeCheck(pyframe)) return nullptr;
  if (!pyframe->pystore->Writable()) return nullptr;

  // Initialize document.
  Frame top(pyframe->pystore->store, pyframe->handle());
  nlp::Document document(top);

  // Parse document.
  parser->Parse(&document);
  document.Update();

  Py_RETURN_NONE;
}

PyObject *PyToLex(PyObject *self, PyObject *args) {
  // Get arguments.
  PyFrame *pyframe;
  if (!PyArg_ParseTuple(args, "O", &pyframe)) return nullptr;
  if (!PyFrame::TypeCheck(pyframe)) return nullptr;

  // Initialize document from frame.
  Frame top(pyframe->pystore->store, pyframe->handle());
  nlp::Document document(top);

  // Convert to LEX.
  string lex = nlp::ToLex(document);

  // Return LEX representation.
  return PyString_FromStringAndSize(lex.data(), lex.size());
}

PyObject *PyEvaluateFrames(PyObject *self, PyObject *args) {
  // Get arguments.
  PyStore *pystore;
  const char *golden;
  const char *test;
  if (!PyArg_ParseTuple(args, "Oss", &pystore, &golden, &test)) return nullptr;
  if (!PyStore::TypeCheck(pystore)) return nullptr;

  // Run frame evaluation.
  nlp::FrameEvaluation::Output eval;
  nlp::FrameEvaluation::Evaluate(pystore->store, golden, test, &eval);
  nlp::FrameEvaluation::Scores scores;
  eval.GetScores(&scores);

  // Return list of benchmark scores.
  PyObject *result = PyList_New(scores.size());
  for (int i = 0; i < scores.size(); ++i) {
    PyObject *score = PyTuple_New(2);
    PyTuple_SetItem(score, 0, PyBase::AllocateString(scores[i].first));
    PyTuple_SetItem(score, 1, PyFloat_FromDouble(scores[i].second));
    PyList_SetItem(result, i, score);
  }

  return result;
}

}  // namespace sling

