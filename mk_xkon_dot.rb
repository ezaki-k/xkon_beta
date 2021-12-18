#!/bin/ruby
require 'digest/sha2'

list=[]
open(ARGV.shift,"r"){|f|
    while l=f.gets
        l=l.sub(/XKON_INSN_NAME\s*\(([^)]+)\)/){$1}
        if %r[//\s*(.)impl\s+(\S*)(?:\s+(.*))?]=~l
           #puts("// "+({:type => $1, :insn=> $2,:info => $3}).inspect)
        elsif  %r|void ([^(]+)\((.*)\)\s*{|=~ l
            info={:insn =>$1, :args=>$2.split(/,\s*/)}
            next unless /_./=~info[:insn]
            info[:name]= info[:insn].split(/_/)
            list<< info 
        end
    end
}

class St
    def initialize(name)
        @name=name
        @children={}
        @methods=[]
    end
    attr_reader :name, :children, :methods
    def add(names,list)
        if names.empty?
            @methods<<list
        else
            n=names[0]
            names=names[1..-1]
            nm= if @name.empty? then n else @name+"_"+n end
            if names.empty?
                @methods<< list
            else
                @children[n]||= St.new(nm)
                @children[n].add(names,list)
            end
        end
    end
    def mkredirect(info)
       as= info[:args].map{|a| a.sub(/\s*=.*/,'').split(/\s+/)[-1]}
       "  constexpr inline void #{info[:name][-1]}(#{info[:args].join(", ")}) const { parent->#{info[:insn]}(#{as.join(', ')}); }"
    end

    def digest
        l=[]
        @children.each{|k,v|
            l<<"c:#{k}=>[#{v.digest}]"
        }
        @methods.each{|m|
            l<<"m:#{m[:name][-1]}(#{m[:args].map{|a| a.sub(/\s*=.*/,'').split(/\s+/)[0..-2].join(' ')}.join(', ')})->#{m[:insn]}"
        }

        str=l.sort.join("|")
        Digest::SHA256.hexdigest(str)[0,4]
    end

    def cls_name
        'DotImpl_'+@name #+digest
    end

    def gen
        if @name.empty?
            s=<<~"EOS"
            private:
            #{@children.map{|k,e| e.gen}.join()}
            public:
            #{@children.map{|k,e| "  #{e.cls_name} #{e.name};\n"}.join()}
            CodeGenerator(std::size_t size = 4096) :
                #{(["Registers()","st(size)"]+@children.keys.map{|e| "#{e}(this)"}).join(", ")}{}
            EOS
            return s
        end
        s=<<~"EOS"
        #{@children.map{|k,e| e.gen}.join()}
        class #{cls_name} {
          friend self_t;
          self_t *parent;
        public:
        #{@children.map{|k,e| "  #{e.cls_name} #{e.name.sub(/^.*_/,'')};\n"}.join()}
        #{@methods.map{|m| mkredirect(m)}.join("\n")}
          #{cls_name}(self_t *p) : 
            #{(["parent(p)"]+@children.keys.map{|e| "#{e}(p)"}).join(", ")}{}
        };
        EOS
        return s
    end
end

struct=St.new('')
list.each{|e|
    struct.add(e[:name],e)
}

puts "// 名前にドットを含む命令の呼び出し用のクラスの定義"
puts "// このファイルは自動生成されたファイルなので変更しないでください"
puts struct.gen
