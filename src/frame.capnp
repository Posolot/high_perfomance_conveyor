@0xd8f5a9d1eab2c411;

struct FrameMeta {
  frameId @0 :UInt64;
  traceId @1 :UInt64;
  splitId @2 :UInt64;
  branchId @3 :UInt16;
  expectedBranches @4 :UInt16;
  sourceStage @5 :Text;
  createdNs @6 :UInt64;
}

struct FrameEnvelope {
  meta @0 :FrameMeta;
  height @1 :UInt32;
  width @2 :UInt32;
  channels @3 :UInt16;
  pixels @4 :Data;
}