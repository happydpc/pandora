
# parsetab.py
# This file is automatically generated. Do not edit.
# pylint: disable=W,C,R
_tabversion = '3.10'

_lr_method = 'LALR'

_lr_signature = 'statements_sceneACCELERATOR AREA_LIGHT_SOURCE ATTRIBUTE_BEGIN ATTRIBUTE_END CAMERA COMMENT CONCAT_TRANSFORM COORDINATE_SYSTEM COORDINATE_SYSTEM_TRANSFORM FILM FILTER IDENTITY INCLUDE INTEGRATOR LIGHT_SOURCE LIST LOOKAT MAKE_NAMED_MATERIAL MATERIAL NAMED_MATERIAL NUMBER OBJECT_BEGIN OBJECT_END OBJECT_INSTANCE REVERSE_ORIENTATION ROTATE SAMPLER SCALE SHAPE STRING TEXTURE TRANSFORM TRANSFORM_BEGIN TRANSFORM_END TRANSLATE WORLD_BEGIN WORLD_ENDstatement_main : statements_config world_begin statements_scene WORLD_ENDbasic_data_type : STRING\n                       | NUMBERstatements_config : statements_config statement_config\n                         | list : LISTstatements_scene : statements_scene statement_scene\n                         | argument : STRING basic_data_type\n                | STRING liststatement_include : INCLUDE STRINGstatement_scene : statement_includestatement_config : statement_includeworld_begin : WORLD_BEGINstatement_scene : ATTRIBUTE_BEGINstatement_scene : ATTRIBUTE_ENDstatement_scene : TRANSFORM_BEGINstatement_scene : TRANSFORM_ENDstatement_scene : statement_transformarguments : arguments argument\n                 | statement_transform : IDENTITYstatement_transform : TRANSLATE NUMBER NUMBER NUMBERstatement_transform : SCALE NUMBER NUMBER NUMBERstatement_transform : ROTATE NUMBER NUMBER NUMBER NUMBERstatement_transform : LOOKAT NUMBER NUMBER NUMBER NUMBER NUMBER NUMBER NUMBER NUMBER NUMBERstatement_transform : COORDINATE_SYSTEM STRINGstatement_transform : COORDINATE_SYSTEM_TRANSFORM STRINGstatement_transform : TRANSFORM liststatement_transform : CONCAT_TRANSFORM liststatement_scene : MATERIAL STRING argumentsstatement_scene : NAMED_MATERIAL STRINGstatement_scene : MAKE_NAMED_MATERIAL STRING argumentsstatement_scene : TEXTURE STRING STRING STRING argumentsstatement_scene : LIGHT_SOURCE STRING argumentsstatement_scene : AREA_LIGHT_SOURCE STRING argumentsstatement_scene : OBJECT_BEGIN STRINGstatement_scene : OBJECT_ENDstatement_scene : OBJECT_INSTANCE STRINGstatement_scene : SHAPE STRING argumentsstatement_scene : REVERSE_ORIENTATIONstatement_config : statement_transformstatement_config : CAMERA STRING argumentsstatement_config : SAMPLER STRING argumentsstatement_config : FILM STRING argumentsstatement_config : FILTER STRING argumentsstatement_config : INTEGRATOR STRING argumentsstatement_config : ACCELERATOR STRING arguments'
    
_lr_action_items = {'ATTRIBUTE_BEGIN':([0,1,2,3,4,5,6,7,8,16,19,21,30,31,32,34,35,36,37,38,39,44,45,46,47,48,49,50,52,53,54,60,61,62,63,66,67,68,69,70,71,77,],[-8,4,-7,-12,-15,-16,-17,-18,-19,-38,-41,-22,-21,-32,-21,-21,-21,-37,-39,-21,-11,-27,-28,-29,-6,-30,-31,-33,-35,-36,-40,-20,-21,-23,-24,-2,-9,-10,-3,-34,-25,-26,]),'ATTRIBUTE_END':([0,1,2,3,4,5,6,7,8,16,19,21,30,31,32,34,35,36,37,38,39,44,45,46,47,48,49,50,52,53,54,60,61,62,63,66,67,68,69,70,71,77,],[-8,5,-7,-12,-15,-16,-17,-18,-19,-38,-41,-22,-21,-32,-21,-21,-21,-37,-39,-21,-11,-27,-28,-29,-6,-30,-31,-33,-35,-36,-40,-20,-21,-23,-24,-2,-9,-10,-3,-34,-25,-26,]),'TRANSFORM_BEGIN':([0,1,2,3,4,5,6,7,8,16,19,21,30,31,32,34,35,36,37,38,39,44,45,46,47,48,49,50,52,53,54,60,61,62,63,66,67,68,69,70,71,77,],[-8,6,-7,-12,-15,-16,-17,-18,-19,-38,-41,-22,-21,-32,-21,-21,-21,-37,-39,-21,-11,-27,-28,-29,-6,-30,-31,-33,-35,-36,-40,-20,-21,-23,-24,-2,-9,-10,-3,-34,-25,-26,]),'TRANSFORM_END':([0,1,2,3,4,5,6,7,8,16,19,21,30,31,32,34,35,36,37,38,39,44,45,46,47,48,49,50,52,53,54,60,61,62,63,66,67,68,69,70,71,77,],[-8,7,-7,-12,-15,-16,-17,-18,-19,-38,-41,-22,-21,-32,-21,-21,-21,-37,-39,-21,-11,-27,-28,-29,-6,-30,-31,-33,-35,-36,-40,-20,-21,-23,-24,-2,-9,-10,-3,-34,-25,-26,]),'MATERIAL':([0,1,2,3,4,5,6,7,8,16,19,21,30,31,32,34,35,36,37,38,39,44,45,46,47,48,49,50,52,53,54,60,61,62,63,66,67,68,69,70,71,77,],[-8,9,-7,-12,-15,-16,-17,-18,-19,-38,-41,-22,-21,-32,-21,-21,-21,-37,-39,-21,-11,-27,-28,-29,-6,-30,-31,-33,-35,-36,-40,-20,-21,-23,-24,-2,-9,-10,-3,-34,-25,-26,]),'NAMED_MATERIAL':([0,1,2,3,4,5,6,7,8,16,19,21,30,31,32,34,35,36,37,38,39,44,45,46,47,48,49,50,52,53,54,60,61,62,63,66,67,68,69,70,71,77,],[-8,10,-7,-12,-15,-16,-17,-18,-19,-38,-41,-22,-21,-32,-21,-21,-21,-37,-39,-21,-11,-27,-28,-29,-6,-30,-31,-33,-35,-36,-40,-20,-21,-23,-24,-2,-9,-10,-3,-34,-25,-26,]),'MAKE_NAMED_MATERIAL':([0,1,2,3,4,5,6,7,8,16,19,21,30,31,32,34,35,36,37,38,39,44,45,46,47,48,49,50,52,53,54,60,61,62,63,66,67,68,69,70,71,77,],[-8,11,-7,-12,-15,-16,-17,-18,-19,-38,-41,-22,-21,-32,-21,-21,-21,-37,-39,-21,-11,-27,-28,-29,-6,-30,-31,-33,-35,-36,-40,-20,-21,-23,-24,-2,-9,-10,-3,-34,-25,-26,]),'TEXTURE':([0,1,2,3,4,5,6,7,8,16,19,21,30,31,32,34,35,36,37,38,39,44,45,46,47,48,49,50,52,53,54,60,61,62,63,66,67,68,69,70,71,77,],[-8,12,-7,-12,-15,-16,-17,-18,-19,-38,-41,-22,-21,-32,-21,-21,-21,-37,-39,-21,-11,-27,-28,-29,-6,-30,-31,-33,-35,-36,-40,-20,-21,-23,-24,-2,-9,-10,-3,-34,-25,-26,]),'LIGHT_SOURCE':([0,1,2,3,4,5,6,7,8,16,19,21,30,31,32,34,35,36,37,38,39,44,45,46,47,48,49,50,52,53,54,60,61,62,63,66,67,68,69,70,71,77,],[-8,13,-7,-12,-15,-16,-17,-18,-19,-38,-41,-22,-21,-32,-21,-21,-21,-37,-39,-21,-11,-27,-28,-29,-6,-30,-31,-33,-35,-36,-40,-20,-21,-23,-24,-2,-9,-10,-3,-34,-25,-26,]),'AREA_LIGHT_SOURCE':([0,1,2,3,4,5,6,7,8,16,19,21,30,31,32,34,35,36,37,38,39,44,45,46,47,48,49,50,52,53,54,60,61,62,63,66,67,68,69,70,71,77,],[-8,14,-7,-12,-15,-16,-17,-18,-19,-38,-41,-22,-21,-32,-21,-21,-21,-37,-39,-21,-11,-27,-28,-29,-6,-30,-31,-33,-35,-36,-40,-20,-21,-23,-24,-2,-9,-10,-3,-34,-25,-26,]),'OBJECT_BEGIN':([0,1,2,3,4,5,6,7,8,16,19,21,30,31,32,34,35,36,37,38,39,44,45,46,47,48,49,50,52,53,54,60,61,62,63,66,67,68,69,70,71,77,],[-8,15,-7,-12,-15,-16,-17,-18,-19,-38,-41,-22,-21,-32,-21,-21,-21,-37,-39,-21,-11,-27,-28,-29,-6,-30,-31,-33,-35,-36,-40,-20,-21,-23,-24,-2,-9,-10,-3,-34,-25,-26,]),'OBJECT_END':([0,1,2,3,4,5,6,7,8,16,19,21,30,31,32,34,35,36,37,38,39,44,45,46,47,48,49,50,52,53,54,60,61,62,63,66,67,68,69,70,71,77,],[-8,16,-7,-12,-15,-16,-17,-18,-19,-38,-41,-22,-21,-32,-21,-21,-21,-37,-39,-21,-11,-27,-28,-29,-6,-30,-31,-33,-35,-36,-40,-20,-21,-23,-24,-2,-9,-10,-3,-34,-25,-26,]),'OBJECT_INSTANCE':([0,1,2,3,4,5,6,7,8,16,19,21,30,31,32,34,35,36,37,38,39,44,45,46,47,48,49,50,52,53,54,60,61,62,63,66,67,68,69,70,71,77,],[-8,17,-7,-12,-15,-16,-17,-18,-19,-38,-41,-22,-21,-32,-21,-21,-21,-37,-39,-21,-11,-27,-28,-29,-6,-30,-31,-33,-35,-36,-40,-20,-21,-23,-24,-2,-9,-10,-3,-34,-25,-26,]),'SHAPE':([0,1,2,3,4,5,6,7,8,16,19,21,30,31,32,34,35,36,37,38,39,44,45,46,47,48,49,50,52,53,54,60,61,62,63,66,67,68,69,70,71,77,],[-8,18,-7,-12,-15,-16,-17,-18,-19,-38,-41,-22,-21,-32,-21,-21,-21,-37,-39,-21,-11,-27,-28,-29,-6,-30,-31,-33,-35,-36,-40,-20,-21,-23,-24,-2,-9,-10,-3,-34,-25,-26,]),'REVERSE_ORIENTATION':([0,1,2,3,4,5,6,7,8,16,19,21,30,31,32,34,35,36,37,38,39,44,45,46,47,48,49,50,52,53,54,60,61,62,63,66,67,68,69,70,71,77,],[-8,19,-7,-12,-15,-16,-17,-18,-19,-38,-41,-22,-21,-32,-21,-21,-21,-37,-39,-21,-11,-27,-28,-29,-6,-30,-31,-33,-35,-36,-40,-20,-21,-23,-24,-2,-9,-10,-3,-34,-25,-26,]),'INCLUDE':([0,1,2,3,4,5,6,7,8,16,19,21,30,31,32,34,35,36,37,38,39,44,45,46,47,48,49,50,52,53,54,60,61,62,63,66,67,68,69,70,71,77,],[-8,20,-7,-12,-15,-16,-17,-18,-19,-38,-41,-22,-21,-32,-21,-21,-21,-37,-39,-21,-11,-27,-28,-29,-6,-30,-31,-33,-35,-36,-40,-20,-21,-23,-24,-2,-9,-10,-3,-34,-25,-26,]),'IDENTITY':([0,1,2,3,4,5,6,7,8,16,19,21,30,31,32,34,35,36,37,38,39,44,45,46,47,48,49,50,52,53,54,60,61,62,63,66,67,68,69,70,71,77,],[-8,21,-7,-12,-15,-16,-17,-18,-19,-38,-41,-22,-21,-32,-21,-21,-21,-37,-39,-21,-11,-27,-28,-29,-6,-30,-31,-33,-35,-36,-40,-20,-21,-23,-24,-2,-9,-10,-3,-34,-25,-26,]),'TRANSLATE':([0,1,2,3,4,5,6,7,8,16,19,21,30,31,32,34,35,36,37,38,39,44,45,46,47,48,49,50,52,53,54,60,61,62,63,66,67,68,69,70,71,77,],[-8,22,-7,-12,-15,-16,-17,-18,-19,-38,-41,-22,-21,-32,-21,-21,-21,-37,-39,-21,-11,-27,-28,-29,-6,-30,-31,-33,-35,-36,-40,-20,-21,-23,-24,-2,-9,-10,-3,-34,-25,-26,]),'SCALE':([0,1,2,3,4,5,6,7,8,16,19,21,30,31,32,34,35,36,37,38,39,44,45,46,47,48,49,50,52,53,54,60,61,62,63,66,67,68,69,70,71,77,],[-8,23,-7,-12,-15,-16,-17,-18,-19,-38,-41,-22,-21,-32,-21,-21,-21,-37,-39,-21,-11,-27,-28,-29,-6,-30,-31,-33,-35,-36,-40,-20,-21,-23,-24,-2,-9,-10,-3,-34,-25,-26,]),'ROTATE':([0,1,2,3,4,5,6,7,8,16,19,21,30,31,32,34,35,36,37,38,39,44,45,46,47,48,49,50,52,53,54,60,61,62,63,66,67,68,69,70,71,77,],[-8,24,-7,-12,-15,-16,-17,-18,-19,-38,-41,-22,-21,-32,-21,-21,-21,-37,-39,-21,-11,-27,-28,-29,-6,-30,-31,-33,-35,-36,-40,-20,-21,-23,-24,-2,-9,-10,-3,-34,-25,-26,]),'LOOKAT':([0,1,2,3,4,5,6,7,8,16,19,21,30,31,32,34,35,36,37,38,39,44,45,46,47,48,49,50,52,53,54,60,61,62,63,66,67,68,69,70,71,77,],[-8,25,-7,-12,-15,-16,-17,-18,-19,-38,-41,-22,-21,-32,-21,-21,-21,-37,-39,-21,-11,-27,-28,-29,-6,-30,-31,-33,-35,-36,-40,-20,-21,-23,-24,-2,-9,-10,-3,-34,-25,-26,]),'COORDINATE_SYSTEM':([0,1,2,3,4,5,6,7,8,16,19,21,30,31,32,34,35,36,37,38,39,44,45,46,47,48,49,50,52,53,54,60,61,62,63,66,67,68,69,70,71,77,],[-8,26,-7,-12,-15,-16,-17,-18,-19,-38,-41,-22,-21,-32,-21,-21,-21,-37,-39,-21,-11,-27,-28,-29,-6,-30,-31,-33,-35,-36,-40,-20,-21,-23,-24,-2,-9,-10,-3,-34,-25,-26,]),'COORDINATE_SYSTEM_TRANSFORM':([0,1,2,3,4,5,6,7,8,16,19,21,30,31,32,34,35,36,37,38,39,44,45,46,47,48,49,50,52,53,54,60,61,62,63,66,67,68,69,70,71,77,],[-8,27,-7,-12,-15,-16,-17,-18,-19,-38,-41,-22,-21,-32,-21,-21,-21,-37,-39,-21,-11,-27,-28,-29,-6,-30,-31,-33,-35,-36,-40,-20,-21,-23,-24,-2,-9,-10,-3,-34,-25,-26,]),'TRANSFORM':([0,1,2,3,4,5,6,7,8,16,19,21,30,31,32,34,35,36,37,38,39,44,45,46,47,48,49,50,52,53,54,60,61,62,63,66,67,68,69,70,71,77,],[-8,28,-7,-12,-15,-16,-17,-18,-19,-38,-41,-22,-21,-32,-21,-21,-21,-37,-39,-21,-11,-27,-28,-29,-6,-30,-31,-33,-35,-36,-40,-20,-21,-23,-24,-2,-9,-10,-3,-34,-25,-26,]),'CONCAT_TRANSFORM':([0,1,2,3,4,5,6,7,8,16,19,21,30,31,32,34,35,36,37,38,39,44,45,46,47,48,49,50,52,53,54,60,61,62,63,66,67,68,69,70,71,77,],[-8,29,-7,-12,-15,-16,-17,-18,-19,-38,-41,-22,-21,-32,-21,-21,-21,-37,-39,-21,-11,-27,-28,-29,-6,-30,-31,-33,-35,-36,-40,-20,-21,-23,-24,-2,-9,-10,-3,-34,-25,-26,]),'$end':([0,1,2,3,4,5,6,7,8,16,19,21,30,31,32,34,35,36,37,38,39,44,45,46,47,48,49,50,52,53,54,60,61,62,63,66,67,68,69,70,71,77,],[-8,0,-7,-12,-15,-16,-17,-18,-19,-38,-41,-22,-21,-32,-21,-21,-21,-37,-39,-21,-11,-27,-28,-29,-6,-30,-31,-33,-35,-36,-40,-20,-21,-23,-24,-2,-9,-10,-3,-34,-25,-26,]),'STRING':([9,10,11,12,13,14,15,17,18,20,26,27,30,32,33,34,35,38,47,49,50,51,52,53,54,59,60,61,66,67,68,69,70,],[30,31,32,33,34,35,36,37,38,39,44,45,-21,-21,51,-21,-21,-21,-6,59,59,61,59,59,59,66,-20,-21,-2,-9,-10,-3,59,]),'NUMBER':([22,23,24,25,40,41,42,43,55,56,57,58,59,64,65,72,73,74,75,76,],[40,41,42,43,55,56,57,58,62,63,64,65,69,71,72,73,74,75,76,77,]),'LIST':([28,29,59,],[47,47,47,]),}

_lr_action = {}
for _k, _v in _lr_action_items.items():
   for _x,_y in zip(_v[0],_v[1]):
      if not _x in _lr_action:  _lr_action[_x] = {}
      _lr_action[_x][_k] = _y
del _lr_action_items

_lr_goto_items = {'statements_scene':([0,],[1,]),'statement_scene':([1,],[2,]),'statement_include':([1,],[3,]),'statement_transform':([1,],[8,]),'list':([28,29,59,],[46,48,68,]),'arguments':([30,32,34,35,38,61,],[49,50,52,53,54,70,]),'argument':([49,50,52,53,54,70,],[60,60,60,60,60,60,]),'basic_data_type':([59,],[67,]),}

_lr_goto = {}
for _k, _v in _lr_goto_items.items():
   for _x, _y in zip(_v[0], _v[1]):
       if not _x in _lr_goto: _lr_goto[_x] = {}
       _lr_goto[_x][_k] = _y
del _lr_goto_items
_lr_productions = [
  ("S' -> statements_scene","S'",1,None,None,None),
  ('statement_main -> statements_config world_begin statements_scene WORLD_END','statement_main',4,'p_statement_main','parser.py',23),
  ('basic_data_type -> STRING','basic_data_type',1,'p_basic_data_type','parser_basics.py',30),
  ('basic_data_type -> NUMBER','basic_data_type',1,'p_basic_data_type','parser_basics.py',31),
  ('statements_config -> statements_config statement_config','statements_config',2,'p_statements_config','parser.py',36),
  ('statements_config -> <empty>','statements_config',0,'p_statements_config','parser.py',37),
  ('list -> LIST','list',1,'p_list','parser_basics.py',39),
  ('statements_scene -> statements_scene statement_scene','statements_scene',2,'p_statements_scene','parser.py',49),
  ('statements_scene -> <empty>','statements_scene',0,'p_statements_scene','parser.py',50),
  ('argument -> STRING basic_data_type','argument',2,'p_argument','parser_basics.py',55),
  ('argument -> STRING list','argument',2,'p_argument','parser_basics.py',56),
  ('statement_include -> INCLUDE STRING','statement_include',2,'p_statement_include','parser.py',58),
  ('statement_scene -> statement_include','statement_scene',1,'p_statements_scene_include','parser.py',88),
  ('statement_config -> statement_include','statement_config',1,'p_statements_config_include','parser.py',93),
  ('world_begin -> WORLD_BEGIN','world_begin',1,'p_statement_world_begin','parser.py',130),
  ('statement_scene -> ATTRIBUTE_BEGIN','statement_scene',1,'p_statement_attribute_begin','parser.py',138),
  ('statement_scene -> ATTRIBUTE_END','statement_scene',1,'p_statement_attribute_end','parser.py',145),
  ('statement_scene -> TRANSFORM_BEGIN','statement_scene',1,'p_statement_transform_begin','parser.py',152),
  ('statement_scene -> TRANSFORM_END','statement_scene',1,'p_statement_transform_end','parser.py',158),
  ('statement_scene -> statement_transform','statement_scene',1,'p_statement_scene_transform','parser.py',164),
  ('arguments -> arguments argument','arguments',2,'p_arguments','parser_basics.py',167),
  ('arguments -> <empty>','arguments',0,'p_arguments','parser_basics.py',168),
  ('statement_transform -> IDENTITY','statement_transform',1,'p_statement_transform_identity','parser.py',169),
  ('statement_transform -> TRANSLATE NUMBER NUMBER NUMBER','statement_transform',4,'p_statement_transform_translate','parser.py',175),
  ('statement_transform -> SCALE NUMBER NUMBER NUMBER','statement_transform',4,'p_statement_transform_scale','parser.py',182),
  ('statement_transform -> ROTATE NUMBER NUMBER NUMBER NUMBER','statement_transform',5,'p_statement_transform_rotate','parser.py',189),
  ('statement_transform -> LOOKAT NUMBER NUMBER NUMBER NUMBER NUMBER NUMBER NUMBER NUMBER NUMBER','statement_transform',10,'p_statement_transform_lookat','parser.py',197),
  ('statement_transform -> COORDINATE_SYSTEM STRING','statement_transform',2,'p_statement_transform_coordinate_system','parser.py',206),
  ('statement_transform -> COORDINATE_SYSTEM_TRANSFORM STRING','statement_transform',2,'p_statement_transform_coord_sys_transform','parser.py',212),
  ('statement_transform -> TRANSFORM list','statement_transform',2,'p_statement_transform','parser.py',218),
  ('statement_transform -> CONCAT_TRANSFORM list','statement_transform',2,'p_statement_transform_concat','parser.py',226),
  ('statement_scene -> MATERIAL STRING arguments','statement_scene',3,'p_statement_material','parser.py',234),
  ('statement_scene -> NAMED_MATERIAL STRING','statement_scene',2,'p_statement_named_material','parser.py',240),
  ('statement_scene -> MAKE_NAMED_MATERIAL STRING arguments','statement_scene',3,'p_statement_make_named_material','parser.py',250),
  ('statement_scene -> TEXTURE STRING STRING STRING arguments','statement_scene',5,'p_statement_texture','parser.py',259),
  ('statement_scene -> LIGHT_SOURCE STRING arguments','statement_scene',3,'p_statement_light','parser.py',274),
  ('statement_scene -> AREA_LIGHT_SOURCE STRING arguments','statement_scene',3,'p_statement_area_light','parser.py',283),
  ('statement_scene -> OBJECT_BEGIN STRING','statement_scene',2,'p_statement_object_begin','parser.py',291),
  ('statement_scene -> OBJECT_END','statement_scene',1,'p_statement_object_end','parser.py',297),
  ('statement_scene -> OBJECT_INSTANCE STRING','statement_scene',2,'p_statement_object_instance','parser.py',304),
  ('statement_scene -> SHAPE STRING arguments','statement_scene',3,'p_statement_shape','parser.py',311),
  ('statement_scene -> REVERSE_ORIENTATION','statement_scene',1,'p_statement_reverse_orientation','parser.py',337),
  ('statement_config -> statement_transform','statement_config',1,'p_statement_config_transform','parser.py',343),
  ('statement_config -> CAMERA STRING arguments','statement_config',3,'p_statement_camera','parser.py',348),
  ('statement_config -> SAMPLER STRING arguments','statement_config',3,'p_statement_sampler','parser.py',358),
  ('statement_config -> FILM STRING arguments','statement_config',3,'p_statement_film','parser.py',367),
  ('statement_config -> FILTER STRING arguments','statement_config',3,'p_statement_filter','parser.py',374),
  ('statement_config -> INTEGRATOR STRING arguments','statement_config',3,'p_statement_integrator','parser.py',381),
  ('statement_config -> ACCELERATOR STRING arguments','statement_config',3,'p_statement_accelerator','parser.py',390),
]
