import hashlib
import json
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class FillDrive(unittest.TestCase):
 def test_timed_motion_and_logical_bridge(self):
  with tempfile.TemporaryDirectory() as directory:
   exe=Path(directory)/'proof'
   command=['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(ROOT/'Core/Inc'),str(ROOT/'Core/Src/fill_motion.c'),str(ROOT/'Core/Src/fill_bridge.c'),str(ROOT/'tests/fill_drive/test_motion.c'),'-o',str(exe)]
   subprocess.run(command,check=True,capture_output=True,text=True)
   env=os.environ.copy();env['ASAN_OPTIONS']='detect_leaks=1'
   subprocess.run([str(exe)],check=True,capture_output=True,text=True,env=env)
 def test_application_schema_and_compatibility(self):
  schema=json.loads((ROOT/'config/sedsnet.json').read_text());header=(ROOT/'Core/Inc/sedsnet_config.h').read_text()
  for name,count,numeric in [('AB_FILL_DRIVE_SETTINGS',32,257107486),('AB_FILL_DRIVE_STATUS',40,174455333)]:
   entry=next(x for x in schema['types'] if x['name']==name)
   self.assertNotIn('id',entry)
   self.assertEqual(entry['element'],{'kind':'Static','data_type':'UInt8','count':count})
   self.assertIn(f'#define SEDS_DT_{name} ((uint32_t){numeric}U)',header)
  command=(ROOT/'Core/Inc/board_commands.h').read_text()
  self.assertIn('CMD_RETRACT_PLUMBING = 8,',command);self.assertIn('CMD_EXTEND_PLUMBING = 15,',command)
